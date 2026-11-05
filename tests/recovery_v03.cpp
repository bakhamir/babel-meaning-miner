#include "bmm/scanner.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string("failed: ")+#x); } while(false)
namespace {
using namespace bmm;
std::string signature(Storage& db,std::int64_t id) {
  std::ostringstream s;
  s.precision(17);
  for(const auto& f:db.findings(id,100000))
    s<<f.page_address<<'|'<<f.start<<'|'<<f.end<<'|'<<f.text<<'|'<<f.classification<<'|'<<f.language_score<<'|'<<f.source_mode<<'|'<<f.source_backend<<'\n';
  for(const auto& p:db.passages(id,100000)) {
    s<<p.page_address<<'|'<<p.start<<'|'<<p.end<<'|'<<p.text<<'|'<<p.classification<<'|'<<p.coherence_score<<'|'<<p.source_mode<<'|'<<p.generator_version<<'\n';
    for(const auto& f:p.sentences) s<<f.page_address<<'|'<<f.start<<'|'<<f.end<<'|'<<f.text<<'|'<<f.classification<<'|'<<f.source_mode<<'|'<<f.source_backend<<'\n';
  }
  return s.str();
}
struct FailingGenerator final:Generator {
  LocalGenerator inner{42,128};
  bool fail=true;
  Page generate(std::uint64_t i)const override {
    if(fail&&i==17) throw std::runtime_error("intentional-worker-failure");
    return inner.generate(i);
  }
  std::string name()const override { return inner.name(); }
  std::string version()const override { return inner.version(); }
};
void deterministic_and_resume() {
  Storage one(":memory:"),many(":memory:"),resumed(":memory:");
  const auto i=one.create_experiment("constrained",42,"{}"),j=many.create_experiment("constrained",42,"{}"),k=resumed.create_experiment("constrained",42,"{}");
  ConstrainedGenerator g(42,256,2);
  const auto a=Analyzer::passages_from_file("data/words.txt",{},{});
  Scanner s;
  ScanConfig c{200,0,1,10,true};
  c.queue_capacity=2;
  c.source_mode="natural"; // Attempted provenance spoof must be ignored.
  const auto x=s.run(g,a,one,i,c);
  c.threads=4;
  const auto y=s.run(g,a,many,j,c);
  CHECK(x.metrics.lexical_matches==y.metrics.lexical_matches);
  CHECK(x.metrics.sentence_candidates==y.metrics.sentence_candidates);
  CHECK(signature(one,i)==signature(many,j));
  const auto ps=one.passages(i,1000);
  CHECK(!ps.empty());
  CHECK(ps[0].source_mode=="constrained");
  CHECK(ps[0].source_backend==g.name());
  CHECK(ps[0].generator_version==g.version());
  for(const auto& f:ps[0].sentences) { CHECK(f.source_mode=="constrained"); CHECK(f.source_backend==g.name()); }

  const auto part=s.run(g,a,resumed,k,c,[&](const ScanStats& st) { if(st.pages>=10) s.cancel(); });
  CHECK(part.pages>=10&&part.pages<=30);
  CHECK(part.status=="cancelled");
  const auto before=resumed.experiment(k);
  CHECK(before.pages_scanned==part.pages);
  c.start_page=part.pages; c.pages=200-part.pages;
  const auto rest=s.run(g,a,resumed,k,c);
  CHECK(rest.status=="completed");
  CHECK(resumed.experiment(k).pages_scanned==200);
  CHECK(resumed.experiment(k).elapsed_seconds>=before.elapsed_seconds);
  CHECK(signature(resumed,k)==signature(one,i));
  CHECK(resumed.integrity_check()=="ok");
  c.start_page=0;
  bool mismatch=false; try { s.run(g,a,resumed,k,c); } catch(const std::exception&) { mismatch=true; }
  CHECK(mismatch);
  c.start_page=200; c.pages=0; c.queue_capacity=257;
  bool bounds=false; try { s.run(g,a,resumed,k,c); } catch(const std::exception&) { bounds=true; }
  CHECK(bounds);
}
void failure_is_resumable() {
  Storage db(":memory:"),reference(":memory:");
  const auto id=db.create_experiment("local",42,"{}"),other=reference.create_experiment("local",42,"{}");
  FailingGenerator g;
  Analyzer a({"a","the","cat"},1,20);
  Scanner s; ScanConfig c{100,0,4,10,true}; c.queue_capacity=2;
  bool propagated=false;
  try { s.run(g,a,db,id,c); } catch(const std::exception& e) { propagated=std::string(e.what())=="intentional-worker-failure"; }
  CHECK(propagated);
  auto e=db.experiment(id);
  CHECK(e.status=="failed");
  CHECK(e.pages_scanned==0||e.pages_scanned==10);
  CHECK(db.integrity_check()=="ok");
  g.fail=false; c.start_page=e.pages_scanned; c.pages=100-e.pages_scanned;
  s.run(g,a,db,id,c);
  c.start_page=0; c.pages=100;
  s.run(g,a,reference,other,c);
  CHECK(signature(db,id)==signature(reference,other));

  Storage callback_db(":memory:");const auto callback_id=callback_db.create_experiment("local",42,"{}");
  bool callback_error=false;
  try { s.run(g,a,callback_db,callback_id,c,[](const ScanStats&) { throw std::runtime_error("callback-failure"); }); }
  catch(const std::exception& ex) { callback_error=std::string(ex.what())=="callback-failure"; }
  CHECK(callback_error);
  CHECK(callback_db.experiment(callback_id).pages_scanned==10);
  CHECK(callback_db.experiment(callback_id).status=="failed");
}
void migrate_actual_v2(const std::filesystem::path& dir) {
  const auto path=dir/"legacy-v2.db";
  sqlite3* raw=nullptr;
  CHECK(sqlite3_open(path.string().c_str(),&raw)==SQLITE_OK);
  const char* ddl=R"(
CREATE TABLE schema_version(version INTEGER NOT NULL);INSERT INTO schema_version VALUES(2);
CREATE TABLE experiments(id INTEGER PRIMARY KEY,backend TEXT NOT NULL,seed INTEGER NOT NULL,configuration TEXT NOT NULL,generator_version TEXT NOT NULL DEFAULT 'splitmix64-v1',started_at TEXT DEFAULT CURRENT_TIMESTAMP,completed_at TEXT,status TEXT,pages_scanned INTEGER DEFAULT 0,elapsed_seconds REAL DEFAULT 0);
CREATE TABLE findings(id INTEGER PRIMARY KEY,experiment_id INTEGER REFERENCES experiments(id),page_address TEXT,start_offset INTEGER,end_offset INTEGER,extracted_text TEXT,lexical_score REAL,language_score REAL,detection_method TEXT,discovered_at TEXT DEFAULT CURRENT_TIMESTAMP,classification TEXT DEFAULT 'WORD',word_count INTEGER DEFAULT 1,grammar_score REAL DEFAULT 0,ngram_score REAL DEFAULT 0,completeness_score REAL DEFAULT 0,semantic_score REAL,scoring_model TEXT DEFAULT 'baseline-untrained',scoring_version TEXT DEFAULT '1',validation_method TEXT DEFAULT 'lexical',source_backend TEXT DEFAULT 'local',verification_status TEXT DEFAULT 'unverified');
CREATE TABLE checkpoints(experiment_id INTEGER PRIMARY KEY REFERENCES experiments(id),last_completed_work_unit INTEGER,updated_at TEXT DEFAULT CURRENT_TIMESTAMP);
INSERT INTO experiments(id,backend,seed,configuration,status,pages_scanned,elapsed_seconds) VALUES(41,'local',42,'{"old":true}','completed',1000,4.25);
INSERT INTO findings(id,experiment_id,page_address,start_offset,end_offset,extracted_text,lexical_score,language_score,detection_method) VALUES(51,41,'local:42:51',17,20,'cat',3,1.25,'dictionary');
INSERT INTO checkpoints(experiment_id,last_completed_work_unit) VALUES(41,1000);
)";
  CHECK(sqlite3_exec(raw,ddl,nullptr,nullptr,nullptr)==SQLITE_OK);
  sqlite3_close(raw);
  {
    Storage upgraded(path.string());
    const auto e=upgraded.experiment(41);
    CHECK(e.configuration=="{\"old\":true}"); CHECK(e.pages_scanned==1000); CHECK(e.elapsed_seconds==4.25);
    const auto rows=upgraded.findings(41);
    CHECK(rows.size()==1&&rows[0].text=="cat"&&rows[0].start==17&&rows[0].end==20);
    CHECK(upgraded.integrity_check()=="ok");
  }
  CHECK(sqlite3_open(path.string().c_str(),&raw)==SQLITE_OK);
  sqlite3_stmt* q=nullptr;
  CHECK(sqlite3_prepare_v2(raw,"SELECT version FROM schema_version",-1,&q,nullptr)==SQLITE_OK);
  CHECK(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0)==3);sqlite3_finalize(q);
  CHECK(sqlite3_prepare_v2(raw,"SELECT last_completed_work_unit FROM checkpoints WHERE experiment_id=41",-1,&q,nullptr)==SQLITE_OK);
  CHECK(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0)==1000);sqlite3_finalize(q);
  sqlite3_close(raw);
}
}
int main() try {
  const auto dir=std::filesystem::temp_directory_path()/("bmm-recovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(std::filesystem::create_directory(dir));
  deterministic_and_resume(); failure_is_resumable(); migrate_actual_v2(dir);
  std::filesystem::remove_all(dir); // uniquely created test-owned directory only
  std::cout<<"exact determinism, cancellation/recovery, exception propagation, queue bounds, v2 migration passed\n";
  return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
