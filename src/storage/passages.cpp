#include "bmm/storage.hpp"
#include "bmm/result_json.hpp"
#include <sqlite3.h>
#include <fstream>
#include <stdexcept>
namespace bmm {
namespace {
void sql(sqlite3*d,const std::string&s){char*e=nullptr;if(sqlite3_exec(d,s.c_str(),nullptr,nullptr,&e)!=SQLITE_OK){std::string m=e?e:"SQLite failure";sqlite3_free(e);throw std::runtime_error(m);}}
struct Statement{sqlite3*d;sqlite3_stmt*s{};Statement(sqlite3*db,const char*q):d(db){if(sqlite3_prepare_v2(d,q,-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(d));}~Statement(){sqlite3_finalize(s);}void text(int n,const std::string&v){sqlite3_bind_text(s,n,v.c_str(),-1,SQLITE_TRANSIENT);}void num(int n,std::int64_t v){sqlite3_bind_int64(s,n,v);}void real(int n,double v){sqlite3_bind_double(s,n,v);}bool next(){int rc=sqlite3_step(s);if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(d));return rc==SQLITE_ROW;}void done(){if(next())throw std::runtime_error("unexpected SQLite row");}std::string str(int i){auto p=sqlite3_column_text(s,i);return p?reinterpret_cast<const char*>(p):"";}};
std::string csv(const std::string&s){std::string out="\"";for(char c:s){out+=c;if(c=='"')out+='"';}return out+'"';}
}
void Storage::migrate_v3(){bool own=sqlite3_get_autocommit(db_)!=0;if(own)sql(db_,"BEGIN IMMEDIATE");try{sql(db_,R"(CREATE TABLE IF NOT EXISTS passages(id INTEGER PRIMARY KEY,experiment_id INTEGER NOT NULL REFERENCES experiments(id),page_address TEXT NOT NULL,start_offset INTEGER NOT NULL,end_offset INTEGER NOT NULL,extracted_text TEXT NOT NULL,classification TEXT NOT NULL,source_mode TEXT NOT NULL,coherence REAL NOT NULL,lexical_continuity REAL,topic_consistency REAL,entity_repetition REAL,pronoun_reference REAL,transition_score REAL,ngram_score REAL,model TEXT,model_version TEXT,validation_method TEXT,verification_status TEXT,UNIQUE(experiment_id,page_address,start_offset,end_offset)); CREATE TABLE IF NOT EXISTS passage_sentences(passage_id INTEGER NOT NULL REFERENCES passages(id),ordinal INTEGER NOT NULL,page_address TEXT,start_offset INTEGER,end_offset INTEGER,extracted_text TEXT,classification TEXT,grammar_score REAL,ngram_score REAL,PRIMARY KEY(passage_id,ordinal)); CREATE INDEX IF NOT EXISTS passage_rank ON passages(experiment_id,coherence DESC); CREATE TABLE IF NOT EXISTS pipeline_metrics(experiment_id INTEGER PRIMARY KEY REFERENCES experiments(id),lexical INTEGER DEFAULT 0,phrases INTEGER DEFAULT 0,sentences INTEGER DEFAULT 0,grammar INTEGER DEFAULT 0,high_score INTEGER DEFAULT 0,passages INTEGER DEFAULT 0); CREATE TABLE IF NOT EXISTS semantic_validations(text_hash TEXT,model_version TEXT,response_json TEXT,explanation TEXT,PRIMARY KEY(text_hash,model_version)); UPDATE schema_version SET version=3;)" );for(auto column:{"source_backend","generator_version"}){Statement columns(db_,"PRAGMA table_info(passages)");bool found=false;while(columns.next())if(columns.str(1)==column)found=true;if(!found)sql(db_,"ALTER TABLE passages ADD COLUMN "+std::string(column)+" TEXT NOT NULL DEFAULT \'\'");}if(own)sql(db_,"COMMIT");}catch(...){if(own)sql(db_,"ROLLBACK");throw;}}
void Storage::save_passages(std::int64_t id,const std::vector<Passage>&ps){
 for(auto&p:ps){Statement s(db_,"INSERT OR IGNORE INTO passages(experiment_id,page_address,start_offset,end_offset,extracted_text,classification,source_mode,coherence,lexical_continuity,topic_consistency,entity_repetition,pronoun_reference,transition_score,ngram_score,model,model_version,validation_method,verification_status,source_backend,generator_version)VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");s.num(1,id);s.text(2,p.page_address);s.num(3,p.start);s.num(4,p.end);s.text(5,p.text);s.text(6,p.classification);s.text(7,p.source_mode);s.real(8,p.coherence_score);s.real(9,p.lexical_continuity);s.real(10,p.topic_consistency);s.real(11,p.entity_repetition);s.real(12,p.pronoun_reference);s.real(13,p.transition_score);s.real(14,p.ngram_score);s.text(15,p.scoring_model);s.text(16,p.scoring_version);s.text(17,p.validation_method);s.text(18,p.verification_status);s.text(19,p.source_backend);s.text(20,p.generator_version);s.done();if(!sqlite3_changes(db_))continue;auto pid=sqlite3_last_insert_rowid(db_);int n=0;for(auto&f:p.sentences){Statement t(db_,"INSERT INTO passage_sentences VALUES(?,?,?,?,?,?,?,?,?)");t.num(1,pid);t.num(2,n++);t.text(3,f.page_address);t.num(4,f.start);t.num(5,f.end);t.text(6,f.text);t.text(7,f.classification);t.real(8,f.grammar_score);t.real(9,f.ngram_score);t.done();}}
}
void Storage::commit_work(std::int64_t id,const PageAnalysis&r,std::uint64_t completed,double elapsed){sql(db_,"BEGIN IMMEDIATE");try{save_batch(id,r.findings);save_passages(id,r.passages);checkpoint(id,completed,elapsed,"running");Statement s(db_,"INSERT INTO pipeline_metrics VALUES(?,?,?,?,?,?,?) ON CONFLICT(experiment_id) DO UPDATE SET lexical=lexical+excluded.lexical,phrases=phrases+excluded.phrases,sentences=sentences+excluded.sentences,grammar=grammar+excluded.grammar,high_score=high_score+excluded.high_score,passages=passages+excluded.passages");s.num(1,id);s.num(2,r.metrics.lexical_matches);s.num(3,r.metrics.phrase_candidates);s.num(4,r.metrics.sentence_candidates);s.num(5,r.metrics.grammar_accepted);s.num(6,r.metrics.high_scoring);s.num(7,r.metrics.passage_candidates);s.done();sql(db_,"COMMIT");}catch(...){sql(db_,"ROLLBACK");throw;}}
Passage Storage::passage(std::int64_t id)const{Statement s(db_,"SELECT id,experiment_id,page_address,start_offset,end_offset,extracted_text,classification,source_mode,coherence,lexical_continuity,topic_consistency,entity_repetition,pronoun_reference,transition_score,ngram_score,model,model_version,validation_method,verification_status,source_backend,generator_version FROM passages WHERE id=?");s.num(1,id);if(sqlite3_step(s.s)!=SQLITE_ROW)throw std::runtime_error("passage not found");Passage p;p.id=id;p.experiment_id=sqlite3_column_int64(s.s,1);p.page_address=s.str(2);p.start=sqlite3_column_int64(s.s,3);p.end=sqlite3_column_int64(s.s,4);p.text=s.str(5);p.classification=s.str(6);p.source_mode=s.str(7);p.coherence_score=sqlite3_column_double(s.s,8);p.lexical_continuity=sqlite3_column_double(s.s,9);p.topic_consistency=sqlite3_column_double(s.s,10);p.entity_repetition=sqlite3_column_double(s.s,11);p.pronoun_reference=sqlite3_column_double(s.s,12);p.transition_score=sqlite3_column_double(s.s,13);p.ngram_score=sqlite3_column_double(s.s,14);p.scoring_model=s.str(15);p.scoring_version=s.str(16);p.validation_method=s.str(17);p.verification_status=s.str(18);p.source_backend=s.str(19);p.generator_version=s.str(20);Statement t(db_,"SELECT page_address,start_offset,end_offset,extracted_text,classification,grammar_score,ngram_score FROM passage_sentences WHERE passage_id=? ORDER BY ordinal");t.num(1,id);while(t.next()){Finding f;f.page_address=t.str(0);f.start=sqlite3_column_int64(t.s,1);f.end=sqlite3_column_int64(t.s,2);f.text=t.str(3);f.classification=t.str(4);f.grammar_score=sqlite3_column_double(t.s,5);f.ngram_score=sqlite3_column_double(t.s,6);f.method="sentence";f.word_count=NGramLanguageModel::tokenize(f.text).size();f.source_mode=p.source_mode;f.source_backend=p.source_backend;f.scoring_model=p.scoring_model;f.scoring_version=p.scoring_version;f.validation_method="finite-grammar-v3";f.verification_status=p.verification_status;p.sentences.push_back(f);}return p;}
std::vector<Passage> Storage::passages(std::int64_t id,std::size_t limit,const std::string&sort)const{if(sort!="coherence"&&sort!="length")throw std::runtime_error("sort must be coherence|length");std::string order=sort=="coherence"?"coherence DESC,length(extracted_text) DESC,id":"length(extracted_text) DESC,coherence DESC,id";Statement s(db_,("SELECT id FROM passages WHERE experiment_id=? ORDER BY "+order+" LIMIT ?").c_str());s.num(1,id);s.num(2,limit);std::vector<Passage> ps;while(s.next())ps.push_back(passage(sqlite3_column_int64(s.s,0)));return ps;}
void Storage::export_passages(std::int64_t id,const std::string&fmt,const std::string&path)const{
 if(fmt!="csv"&&fmt!="json")throw std::runtime_error("format must be json or csv");
 std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("cannot open export");
 Statement q(db_,"SELECT id FROM passages WHERE experiment_id=? ORDER BY id");q.num(1,id);
 if(fmt=="json")out<<'[';else out<<"id,source_mode,page_address,start_offset,end_offset,text,classification,sentence_count,coherence,source_backend,generator_version,scoring_model,scoring_version,validation_method,verification_status,details_json\n";
 bool first=true;while(q.next()){
  auto p=passage(sqlite3_column_int64(q.s,0));auto json=passage_json(p);
  if(fmt=="csv")out<<p.id<<','<<csv(p.source_mode)<<','<<csv(p.page_address)<<','<<p.start<<','<<p.end<<','<<csv(p.text)<<','<<csv(p.classification)<<','<<p.sentences.size()<<','<<p.coherence_score<<','<<csv(p.source_backend)<<','<<csv(p.generator_version)<<','<<csv(p.scoring_model)<<','<<csv(p.scoring_version)<<','<<csv(p.validation_method)<<','<<csv(p.verification_status)<<','<<csv(json)<<'\n';
  else{if(!first)out<<',';out<<json;first=false;}
 }if(fmt=="json")out<<"]\n";if(!out)throw std::runtime_error("export write failed");
}
void Storage::export_results(std::int64_t id,const std::string&fmt,const std::string&path)const{
 if(fmt!="csv"&&fmt!="json")throw std::runtime_error("format must be json or csv");
 Statement q(db_,"SELECT page_address,start_offset,end_offset,extracted_text,classification,word_count,lexical_score,language_score,grammar_score,ngram_score,completeness_score,semantic_score,scoring_model,scoring_version,validation_method,source_mode,source_backend,verification_status FROM findings WHERE experiment_id=? ORDER BY language_score DESC,id");q.num(1,id);
 std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("cannot open export");
 int columns=sqlite3_column_count(q.s);
 if(fmt=="json")out<<'[';
 else{for(int c=0;c<columns;++c){if(c)out<<',';out<<sqlite3_column_name(q.s,c);}out<<'\n';}
 bool first=true;while(q.next()){
  if(fmt=="json"){if(!first)out<<',';out<<'{';}
  for(int c=0;c<columns;++c){
   if(c)out<<',';
   std::string name=sqlite3_column_name(q.s,c);
   if(fmt=="json")out<<json_quote(name=="extracted_text"?"text":name)<<':';
   int type=sqlite3_column_type(q.s,c);
   if(type==SQLITE_NULL){if(fmt=="json")out<<"null";}
   else if(type==SQLITE_INTEGER)out<<sqlite3_column_int64(q.s,c);
   else if(type==SQLITE_FLOAT)out<<sqlite3_column_double(q.s,c);
   else out<<(fmt=="json"?json_quote(q.str(c)):csv(q.str(c)));
  }
  if(fmt=="json")out<<'}';else out<<'\n';first=false;
 }
 if(fmt=="json")out<<"]\n";if(!out)throw std::runtime_error("export write failed");
}
std::string Storage::integrity_check()const{Statement s(db_,"PRAGMA integrity_check");if(sqlite3_step(s.s)!=SQLITE_ROW)throw std::runtime_error("integrity check failed");return s.str(0);}
std::string Storage::occurrences_json(std::int64_t id,const std::string&type,std::size_t limit)const{
 const char*where=nullptr;
 if(type=="word")where="classification='WORD'";
 else if(type=="sentence")where="detection_method='sentence' AND classification='GRAMMATICAL_SENTENCE'";
 else throw std::runtime_error("occurrence type must be word or sentence");
 std::string query="SELECT extracted_text,classification,source_mode,count(*) FROM findings WHERE experiment_id=? AND "+std::string(where)+" GROUP BY extracted_text,classification,source_mode ORDER BY count(*) DESC,length(extracted_text) DESC,extracted_text LIMIT ?";
 Statement rows(db_,query.c_str());rows.num(1,id);rows.num(2,limit);
 std::ostringstream out;out<<'[';bool first=true;
 while(rows.next()){
  if(!first)out<<',';first=false;
  out<<"{\"text\":"<<json_quote(rows.str(0))<<",\"classification\":"<<json_quote(rows.str(1))<<",\"source_mode\":"<<json_quote(rows.str(2))<<",\"count\":"<<sqlite3_column_int64(rows.s,3)<<'}';
 }
 out<<']';return out.str();
}
std::string Storage::summary_json(std::int64_t id)const{
 auto e=experiment(id);
 Statement s(db_,"SELECT lexical,phrases,sentences,grammar,high_score,passages FROM pipeline_metrics WHERE experiment_id=?");s.num(1,id);bool row=s.next();
 std::ostringstream o;o<<"{\"experiment\":"<<id<<",\"status\":"<<json_quote(e.status)<<",\"backend\":"<<json_quote(e.backend)<<",\"configuration\":"<<json_quote(e.configuration)<<",\"pages_scanned\":"<<e.pages_scanned<<",\"elapsed_seconds\":"<<e.elapsed_seconds<<",\"pages_per_second\":"<<(e.elapsed_seconds?e.pages_scanned/e.elapsed_seconds:0)<<",\"metrics\":{";
 const char*names[]={"lexical","phrases","sentence_candidates","grammar_accepted","high_score","passages"};
 for(int i=0;i<6;++i){if(i)o<<',';o<<json_quote(names[i])<<':'<<(row?sqlite3_column_int64(s.s,i):0);}
 o<<"},\"longest_sentence\":";
 Statement l(db_,"SELECT page_address,start_offset,end_offset,extracted_text,word_count,grammar_score,ngram_score,source_mode FROM findings WHERE experiment_id=? AND classification='GRAMMATICAL_SENTENCE' ORDER BY length(extracted_text) DESC,id LIMIT 1");l.num(1,id);
 if(l.next()){Finding f;f.page_address=l.str(0);f.start=sqlite3_column_int64(l.s,1);f.end=sqlite3_column_int64(l.s,2);f.text=l.str(3);f.word_count=sqlite3_column_int64(l.s,4);f.grammar_score=sqlite3_column_double(l.s,5);f.ngram_score=sqlite3_column_double(l.s,6);f.source_mode=l.str(7);f.classification="GRAMMATICAL_SENTENCE";o<<finding_json(f);}else o<<"null";
 auto record=[&](const char*name,const char*filter,const char*order){
  Statement p(db_,(std::string("SELECT id FROM passages WHERE experiment_id=? ")+filter+" ORDER BY "+order+" LIMIT 1").c_str());p.num(1,id);
  bool found=p.next();o<<','<<json_quote(name)<<':';if(found)o<<passage_json(passage(sqlite3_column_int64(p.s,0)));else o<<"null";
 };
 record("longest_passage","","length(extracted_text) DESC,id");
 record("highest_scoring_passage","","coherence DESC,ngram_score DESC,id");
 record("highest_scoring_coherent_passage","AND classification='COHERENT_PASSAGE'","coherence DESC,ngram_score DESC,id");
 Statement counts(db_,"SELECT classification,count(*) FROM passages WHERE experiment_id=? GROUP BY classification");counts.num(1,id);o<<",\"passage_classes\":{";bool first=true;while(counts.next()){if(!first)o<<',';o<<json_quote(counts.str(0))<<':'<<sqlite3_column_int64(counts.s,1);first=false;}o<<"}}";return o.str();
}
}
