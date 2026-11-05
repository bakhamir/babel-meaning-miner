#include "bmm/scanner.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace bmm {
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t) { return std::chrono::duration<double>(Clock::now()-t).count(); }
void add(AnalysisMetrics& a,const AnalysisMetrics& b) {
  a.lexical_matches+=b.lexical_matches; a.phrase_candidates+=b.phrase_candidates;
  a.sentence_candidates+=b.sentence_candidates; a.grammar_accepted+=b.grammar_accepted;
  a.high_scoring+=b.high_scoring; a.passage_candidates+=b.passage_candidates;
  a.grammatical_passages+=b.grammatical_passages; a.coherent_passages+=b.coherent_passages;
  a.lexical_seconds+=b.lexical_seconds; a.candidate_seconds+=b.candidate_seconds;
  a.grammar_seconds+=b.grammar_seconds; a.ngram_seconds+=b.ngram_seconds;
  a.passage_seconds+=b.passage_seconds;
}
}

ScanStats Scanner::run(const Generator& g,const Analyzer& az,Storage& db,std::int64_t id,
                      const ScanConfig& c,ProgressCallback progress) {
  if(!c.threads||c.threads>256||!c.batch_size||c.batch_size>10000||
     !c.queue_capacity||c.queue_capacity>256)
    throw std::invalid_argument("threads/queue must be 1..256 and batch-size 1..10000");
  const auto old=db.experiment(id);
  if(c.start_page!=old.pages_scanned) throw std::runtime_error("resume checkpoint mismatch");
  if(old.backend!=g.name()) throw std::runtime_error("experiment/source backend mismatch");
  constexpr auto max_cursor=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if(c.start_page>max_cursor||c.pages>max_cursor-c.start_page)
    throw std::overflow_error("page range exceeds SQLite signed 64-bit cursor");

  cancelled_=false;
  const auto began=Clock::now();
  const auto blocks=c.pages/c.batch_size+(c.pages%c.batch_size!=0);
  // Claimed work, including pages currently being analyzed, is bounded by the
  // commit window. A slow first block cannot produce an unbounded reorder map.
  std::mutex mu;
  std::condition_variable cv;
  std::uint64_t claimed=0,committed=0;
  unsigned active=0;
  std::exception_ptr failure;
  struct Work { PageAnalysis result; std::uint64_t pages{},chars{}; double generation{},waiting{}; };
  std::map<std::uint64_t,Work> ready;
  ScanStats stats;
  const auto source_mode=g.source_mode(),backend=g.name(),generator_version=g.version();
  auto tag=[&](Finding& f) { f.source_mode=source_mode; f.source_backend=backend; };
  auto worker=[&] {
    try {
      for(;;) {
        std::uint64_t block;
        double wait;
        {
          const auto t=Clock::now();
          std::unique_lock lk(mu);
          cv.wait_for(lk,std::chrono::milliseconds(50),[&] {
            return cancelled_||failure||claimed>=blocks||claimed-committed<c.queue_capacity;
          });
          wait=elapsed(t);
          if(cancelled_||failure||claimed>=blocks) break;
          if(claimed-committed>=c.queue_capacity) continue;
          block=claimed++;
        }
        Work w;
        w.waiting=wait;
        const auto begin=block*c.batch_size;
        const auto length=std::min<std::uint64_t>(c.batch_size,c.pages-begin);
        // Finish a claimed block on cancellation: only complete contiguous
        // blocks advance the durable cursor. Failure discards uncommitted work.
        for(std::uint64_t offset=0;offset<length;++offset) {
          const auto t=Clock::now();
          auto p=g.generate(c.start_page+begin+offset);
          w.generation+=elapsed(t);
          auto r=az.analyze_page(p.text,p.address);
          for(auto& f:r.findings) tag(f);
          for(auto& passage:r.passages) {
            passage.source_mode=source_mode;
            passage.source_backend=backend;
            passage.generator_version=generator_version;
            for(auto& sentence:passage.sentences) tag(sentence);
          }
          if(!c.store_words) std::erase_if(r.findings,[](const Finding& f) { return f.classification=="WORD"; });
          w.result.findings.insert(w.result.findings.end(),std::make_move_iterator(r.findings.begin()),std::make_move_iterator(r.findings.end()));
          w.result.passages.insert(w.result.passages.end(),std::make_move_iterator(r.passages.begin()),std::make_move_iterator(r.passages.end()));
          add(w.result.metrics,r.metrics);
          ++w.pages; w.chars+=p.text.size();
        }
        {
          std::lock_guard lk(mu);
          ready.emplace(block,std::move(w));
        }
        cv.notify_all();
      }
    } catch(...) {
      std::lock_guard lk(mu);
      if(!failure) failure=std::current_exception();
      cancelled_=true;
    }
    {
      std::lock_guard lk(mu);
      --active;
    }
    cv.notify_all();
  };

  std::vector<std::jthread> threads;
  try {
    threads.reserve(c.threads);
    for(unsigned i=0;i<c.threads&&blocks;++i) {
      {
        std::lock_guard lk(mu); ++active;
      }
      try { threads.emplace_back(worker); }
      catch(...) { std::lock_guard lk(mu); --active; throw; }
    }
    // This coordinator is the sole database writer. No worker acquires SQLite
    // locks, and insertion order is independent of worker scheduling.
    while(committed<blocks) {
      Work w;
      {
        std::unique_lock lk(mu);
        cv.wait(lk,[&] { return failure||ready.contains(committed)||active==0; });
        if(failure) std::rethrow_exception(failure);
        auto it=ready.find(committed);
        if(it==ready.end()) break;
        w=std::move(it->second); ready.erase(it);
      }
      const auto t=Clock::now();
      db.commit_work(id,w.result,c.start_page+stats.pages+w.pages,old.elapsed_seconds+elapsed(began));
      stats.database_seconds+=elapsed(t);
      stats.pages+=w.pages; stats.characters+=w.chars;
      stats.candidates+=w.result.findings.size(); stats.passages+=w.result.passages.size();
      stats.generation_seconds+=w.generation; stats.queue_wait_seconds+=w.waiting;
      add(stats.metrics,w.result.metrics);
      for(const auto& f:w.result.findings)
        if(f.text.size()>stats.best.size()||(f.text.size()==stats.best.size()&&f.text<stats.best)) stats.best=f.text;
      stats.seconds=elapsed(began);
      stats.pages_per_second=stats.seconds?stats.pages/stats.seconds:0;
      stats.chars_per_second=stats.seconds?stats.characters/stats.seconds:0;
      {
        std::lock_guard lk(mu); ++committed;
      }
      cv.notify_all();
      if(progress) progress(stats);
    }
    threads.clear();
  } catch(...) {
    const auto original=std::current_exception();
    cancelled_=true; cv.notify_all(); threads.clear();
    // Preserve the original worker/writer failure even if recording its status
    // also fails (e.g. disk full). The last atomic commit remains resumable.
    try { db.checkpoint(id,c.start_page+stats.pages,old.elapsed_seconds+elapsed(began),"failed"); }
    catch(...) {}
    std::rethrow_exception(original);
  }
  stats.seconds=elapsed(began);
  stats.pages_per_second=stats.seconds?stats.pages/stats.seconds:0;
  stats.chars_per_second=stats.seconds?stats.characters/stats.seconds:0;
  stats.status=stats.pages==c.pages?"completed":"cancelled";
  db.checkpoint(id,c.start_page+stats.pages,old.elapsed_seconds+stats.seconds,stats.status);
  return stats;
}
}
