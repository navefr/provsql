extern "C" {
#include "postgres.h"
#include "fmgr.h"
#include "catalog/pg_type.h"
#include "utils/uuid.h"
#include "executor/spi.h"
#include "provsql_utils.h"
  
  PG_FUNCTION_INFO_V1(save_circuit);
}

#include <csignal>

#include "BooleanCircuit.h"
#include "provsql_utils_cpp.h"
#include "dDNNFTreeDecompositionBuilder.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>

using namespace std;

static void provsql_sigint_handler (int)
{
  provsql_interrupted = true;
}

static Datum save_circuit_internal
  (Datum token, Datum token2prob, string filename)
{
  constants_t constants;
  if(!initialize_constants(&constants)) {
    elog(ERROR, "Cannot find provsql schema");
  }

  Datum arguments[2]={token,token2prob};
  Oid argtypes[2]={constants.OID_TYPE_PROVENANCE_TOKEN,REGCLASSOID};
  char nulls[2] = {' ',' '};
  
  SPI_connect();

  BooleanCircuit c;

  std::vector<string> tuples;
  std::vector<int> tuples_ids;
  std::vector<string> gates;
  std::vector<int> gates_ids;

  if(SPI_execute_with_args(
      "SELECT * FROM provsql.sub_circuit_with_desc($1,$2)", 2, argtypes, arguments, nulls, true, 0)
      == SPI_OK_SELECT) {
    int proc = SPI_processed;
    TupleDesc tupdesc = SPI_tuptable->tupdesc;
    SPITupleTable *tuptable = SPI_tuptable;

    for (int i = 0; i < proc; i++)
    {
      HeapTuple tuple = tuptable->vals[i];

      string f = SPI_getvalue(tuple, tupdesc, 1);
      string type = SPI_getvalue(tuple, tupdesc, 3);
      if(type == "input") {
          c.setGate(f, BooleanGate::IN);

        tuples.push_back(f);
        tuples_ids.push_back(static_cast<int>(c.getGate(f))+1);
      } else {
        auto id=c.getGate(f);

        if(type == "monus" || type == "monusl" || type == "times" || type=="project" || type=="eq") {
          c.setGate(f, BooleanGate::AND);
        } else if(type == "plus") {
          c.setGate(f, BooleanGate::OR);
        } else if(type == "monusr") {
          c.setGate(f, BooleanGate::NOT);
        } else {
          elog(ERROR, "Wrong type of gate in circuit (%s)", type.c_str());
        }
        c.addWire(id, c.getGate(SPI_getvalue(tuple, tupdesc, 2)));

        gates.push_back(f);
        gates_ids.push_back(static_cast<int>(id)+1);
      }
    }
  }

  SPI_finish();

  // Display the circuit for debugging:
  //elog(WARNING, "%s", c.toString(c.getGate(UUIDDatum2string(token))).c_str());

  auto gate = c.getGate(UUIDDatum2string(token));

  provsql_interrupted = false;

  void (*prev_sigint_handler)(int);
  prev_sigint_handler = signal(SIGINT, provsql_sigint_handler);

  

  try {
    filename = c.Save_Tseytin(gate, filename);
  } catch(CircuitException &e) {
    elog(ERROR, "%s", e.what());
  }

  provsql_interrupted = false;
  signal (SIGINT, prev_sigint_handler);
  

  // WRITE tuples
  char cfilename_tuples[filename.length() + 7];
  strcpy(cfilename_tuples, filename.c_str());
  cfilename_tuples[filename.length()] = '_';
  cfilename_tuples[filename.length() + 1] = 't';
  cfilename_tuples[filename.length() + 2] = 'u';
  cfilename_tuples[filename.length() + 3] = 'p';
  cfilename_tuples[filename.length() + 4] = 'l';
  cfilename_tuples[filename.length() + 5] = 'e';
  cfilename_tuples[filename.length() + 6] = '\0';
  string filename_tuples = cfilename_tuples;
  std::ofstream ofs_tuples(filename_tuples.c_str());
  for(unsigned i=0; i<tuples_ids.size(); ++i) {
    ofs_tuples << tuples_ids[i] << " " << tuples[i] << "\n";
  }
  ofs_tuples.close();

  // WRITE gates
  char cfilename_gates[filename.length() + 6];
  strcpy(cfilename_gates, filename.c_str());
  cfilename_gates[filename.length()] = '_';
  cfilename_gates[filename.length() + 1] = 'g';
  cfilename_gates[filename.length() + 2] = 'a';
  cfilename_gates[filename.length() + 3] = 't';
  cfilename_gates[filename.length() + 4] = 'e';
  cfilename_gates[filename.length() + 5] = '\0';
  string filename_gates = cfilename_gates;
  std::ofstream ofs_gates(filename_gates.c_str());
  for(unsigned i=0; i<gates_ids.size(); ++i) {
    ofs_gates << gates_ids[i] << " " << gates[i] << "\n";
  }
  ofs_gates.close();

  text *result = (text *) palloc(VARHDRSZ + filename.size() + 1);
  SET_VARSIZE(result, VARHDRSZ + filename.size());

  memcpy((void *) VARDATA(result), filename.c_str(), filename.size());
  PG_RETURN_TEXT_P(result);
}

Datum save_circuit(PG_FUNCTION_ARGS)
{
  try {
    Datum token = PG_GETARG_DATUM(0);
    Datum token2prob = PG_GETARG_DATUM(1);
    string filename;

    if(!PG_ARGISNULL(2)) {
      text *t = PG_GETARG_TEXT_P(2);
      filename = string(VARDATA(t),VARSIZE(t)-VARHDRSZ);
    }

    if(PG_ARGISNULL(1))
      PG_RETURN_NULL();

    return save_circuit_internal(token, token2prob, filename);
  } catch(const std::exception &e) {
    elog(ERROR, "save_circuit_evaluate: %s", e.what());
  } catch(...) {
    elog(ERROR, "save_circuit_evaluate: Unknown exception");
  }

  PG_RETURN_NULL();
}
