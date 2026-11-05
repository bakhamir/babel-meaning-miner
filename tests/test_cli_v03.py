"""End-to-end JSON/export/resume contract; all databases live in a fresh temp dir."""
import json
import csv
from contextlib import closing
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest

EXE = str(pathlib.Path(sys.argv.pop(1)).resolve())
def run(*args, check=True):
    return subprocess.run([EXE, *map(str, args)], capture_output=True, text=True, check=check)

class CliTests(unittest.TestCase):
    def test_exports_and_resume(self):
        with tempfile.TemporaryDirectory() as d:
            db = pathlib.Path(d)/"test.db"
            run("scan", "--mode", "passages", "--source-mode", "constrained", "--pages", 5, "--db", db, "--json")
            for fmt in ("json", "csv"):
                out=pathlib.Path(d)/("out."+fmt)
                run("export", "--type", "passages", "--experiment", 1, "--db", db, "--format", fmt, "--output", out)
                self.assertTrue(out.stat().st_size)
            rows=json.loads((pathlib.Path(d)/"out.json").read_text(encoding="utf-8"))
            self.assertEqual(len(rows),5); self.assertEqual(rows[0]["source_mode"],"constrained")
            sentences=json.loads(run("occurrences","--type","sentence","--experiment",1,"--db",db,"--limit",100).stdout)
            self.assertEqual(sum(row["count"] for row in sentences),10)
            self.assertTrue(all(row["classification"]=="GRAMMATICAL_SENTENCE" for row in sentences))
            generated={}
            for i in range(5):
                page=json.loads(run("generate","--source-mode","constrained","--page",i,"--json").stdout)
                generated[page.get("page_address",page.get("address"))]=page["text"]
            for row in rows:
                self.assertEqual(row["source_backend"],"constrained")
                self.assertEqual(row["generator_version"],"finite-grammar-v1")
                page=generated[row["page_address"]]
                self.assertEqual(page[row["start_offset"]:row["end_offset"]],row["text"])
                for sentence in row["sentences"]:
                    self.assertEqual(sentence["source_mode"],"constrained")
                    self.assertEqual(sentence["source_backend"],"constrained")
                    self.assertEqual(sentence["page_address"],row["page_address"])
                    self.assertEqual(page[sentence["start_offset"]:sentence["end_offset"]],sentence["text"])
            run("resume", "--experiment", 1, "--pages", 3, "--db", db, "--json")
            stats=json.loads(run("stats", "--experiment", 1, "--db", db).stdout)
            self.assertEqual(stats["pages_scanned"],8)
            self.assertNotEqual(run("resume", "--experiment", 1, "--db", db, "--source-mode", "natural",check=False).returncode,0)
            with closing(sqlite3.connect(db)) as con:
                self.assertEqual(con.execute("pragma integrity_check").fetchone()[0],"ok")
                self.assertEqual(con.execute("select count(*) from passages").fetchone()[0],8)
    def test_findings_escape_and_provenance(self):
        with tempfile.TemporaryDirectory() as d:
            db=pathlib.Path(d)/"escaping.db"
            run("scan","--pages",0,"--db",db,"--json")
            address='test:quote"\\\t\nsource'
            text='quoted "word", slash\\ newline\ncarriage\r tab\t control\x01'
            with closing(sqlite3.connect(db)) as con:
                con.execute("INSERT INTO findings(experiment_id,page_address,start_offset,end_offset,extracted_text,lexical_score,language_score,detection_method,classification,word_count,grammar_score,ngram_score,completeness_score,semantic_score,scoring_model,scoring_version,validation_method,source_backend,source_mode,verification_status) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (1,address,7,7+len(text),text,1.,.5,"synthetic-export-fixture","SENTENCE_CANDIDATE",4,0.,-2.,0.,None,'fixture"model',"test-v1",'manual, "fixture"',"injected-fixture","injection","fixture-only"))
                for offset in (100,200):
                    con.execute("INSERT INTO findings(experiment_id,page_address,start_offset,end_offset,extracted_text,lexical_score,language_score,detection_method,classification,word_count,grammar_score,ngram_score,completeness_score,semantic_score,scoring_model,scoring_version,validation_method,source_backend,source_mode,verification_status) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        (1,"frequency-fixture",offset,offset+3,"cat",3.,1.,"dictionary","WORD",1,0.,0.,0.,None,"baseline-untrained","1","lexical","fixture","injection","fixture-only"))
                con.commit()
            words=json.loads(run("occurrences","--type","word","--experiment",1,"--db",db,"--limit",100).stdout)
            self.assertEqual(words,[{"text":"cat","classification":"WORD","source_mode":"injection","count":2}])
            listed=json.loads(run("findings","--type","word","--experiment",1,"--db",db,"--limit",100).stdout)
            self.assertEqual(len(listed),2)
            for fmt in ("json","csv"):
                out=pathlib.Path(d)/("findings."+fmt)
                run("export","--experiment",1,"--db",db,"--format",fmt,"--output",out)
                with out.open(encoding="utf-8",newline="") as f:
                    rows=json.load(f) if fmt=="json" else list(csv.DictReader(f))
                self.assertEqual(len(rows),3)
                row=next(row for row in rows if row.get("text",row.get("extracted_text"))==text)
                self.assertEqual(row["page_address"],address)
                self.assertEqual(row.get("text",row.get("extracted_text")),text)
                self.assertEqual(row["source_mode"],"injection")
                self.assertEqual(row["source_backend"],"injected-fixture")
                self.assertEqual(row["verification_status"],"fixture-only")
                self.assertEqual(row["scoring_model"],'fixture"model')
    def test_fingerprint_mismatch_refuses_resume(self):
        with tempfile.TemporaryDirectory() as d:
            db=pathlib.Path(d)/"fingerprint.db"
            dictionary=pathlib.Path(d)/"words.txt"
            dictionary.write_text("cat\ndog\nthe\n",encoding="ascii")
            run("scan","--dictionary",dictionary,"--pages",2,"--db",db,"--json")
            before=json.loads(run("stats","--experiment",1,"--db",db).stdout)
            dictionary.write_text("cat\ndog\nthe\nbird\n",encoding="ascii")
            rejected=run("resume","--experiment",1,"--db",db,"--pages",2,check=False)
            self.assertNotEqual(rejected.returncode,0)
            self.assertIn("input changed",rejected.stderr)
            after=json.loads(run("stats","--experiment",1,"--db",db).stdout)
            self.assertEqual(after["pages_scanned"],before["pages_scanned"])
    def test_crash_recovery(self):
        with tempfile.TemporaryDirectory() as d:
            db=pathlib.Path(d)/"crash.db"
            proc=subprocess.Popen([EXE,"scan","--pages","1000000","--mode","passages","--batch-size","10","--threads","2","--db",str(db),"--quiet"],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            committed=0
            try:
                for _ in range(200):
                    if db.exists():
                        try:
                            with closing(sqlite3.connect(f"file:{db.as_posix()}?mode=ro",uri=True,timeout=.1)) as con:
                                committed=con.execute("select pages_scanned from experiments where id=1").fetchone()[0]
                            if committed>=10: break
                        except (sqlite3.Error,TypeError): pass
                    time.sleep(.025)
            finally:
                proc.kill(); proc.wait(timeout=10)
                if proc.stderr: proc.stderr.close()
            self.assertGreaterEqual(committed,10)
            before=json.loads(run("stats","--experiment",1,"--db",db).stdout)["pages_scanned"]
            run("resume","--experiment",1,"--db",db,"--pages",20,"--json")
            after=json.loads(run("stats","--experiment",1,"--db",db).stdout)["pages_scanned"]
            self.assertEqual(after,before+20)
            reference=pathlib.Path(d)/"reference.db"
            run("scan","--pages",after,"--mode","passages","--batch-size",10,"--threads",1,"--db",reference,"--quiet")
            query="SELECT page_address,start_offset,end_offset,extracted_text,classification,source_mode FROM findings ORDER BY page_address,start_offset,end_offset,classification"
            with closing(sqlite3.connect(db)) as recovered, closing(sqlite3.connect(reference)) as baseline:
                self.assertEqual(recovered.execute(query).fetchall(),baseline.execute(query).fetchall())
                self.assertEqual(recovered.execute("SELECT lexical,phrases,sentences,grammar,high_score,passages FROM pipeline_metrics").fetchall(),baseline.execute("SELECT lexical,phrases,sentences,grammar,high_score,passages FROM pipeline_metrics").fetchall())

if __name__=="__main__": unittest.main()
