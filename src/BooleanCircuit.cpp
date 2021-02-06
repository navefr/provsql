#include "BooleanCircuit.h"

extern "C" {
#include <unistd.h>
#include <math.h>
}

#include <cassert>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>

#include "dDNNF.h"

// "provsql_utils.h"
#ifdef TDKC
constexpr bool provsql_interrupted = false;
#else
#include "provsql_utils.h"
#endif

gate_t BooleanCircuit::setGate(BooleanGate type)
{
 auto id = Circuit::setGate(type);
  if(type == BooleanGate::IN) {
    setProb(id,1.);
    inputs.insert(id);
  }
  return id;
}

gate_t BooleanCircuit::setGate(const uuid &u, BooleanGate type)
{
 auto id = Circuit::setGate(u, type);
  if(type == BooleanGate::IN) {
    setProb(id,1.);
    inputs.insert(id);
  }
  return id;
}

gate_t BooleanCircuit::setGate(const uuid &u, BooleanGate type, double p)
{
  auto id = setGate(u, type);
  setProb(id,p);
  return id;
}

gate_t BooleanCircuit::setGate(BooleanGate type, double p)
{
  auto id = setGate(type);
  setProb(id,p);
  return id;
}

gate_t BooleanCircuit::addGate()
{
  auto id=Circuit::addGate();
  prob.push_back(1);
  return id;
}

std::string BooleanCircuit::toString(gate_t g) const
{
  std::string op;
  std::string result;

  switch(getGateType(g)) {
    case BooleanGate::IN:
      if(getProb(g)==0.) {
        return "⊥";
      } else if(getProb(g)==1.) {
        return "⊤";
      } else {
        return to_string(g)+"["+std::to_string(getProb(g))+"]";
      }
    case BooleanGate::NOT:
      op="¬";
      break;
    case BooleanGate::UNDETERMINED:
      op="?";
      break;
    case BooleanGate::AND:
      op="∧";
      break;
    case BooleanGate::OR:
      op="∨";
      break;
  }

  if(getWires(g).empty()) {
    if(getGateType(g)==BooleanGate::AND)
      return "⊤";
    else if(getGateType(g)==BooleanGate::OR)
      return "⊥";
    else return op;
  }

  for(auto s: getWires(g)) {
    if(getGateType(g)==BooleanGate::NOT)
      result = op;
    else if(!result.empty())
      result+=" "+op+" ";
    result+=toString(s);
  }

  return "("+result+")";
}

bool BooleanCircuit::evaluate(gate_t g, const std::unordered_set<gate_t> &sampled) const
{
  bool disjunction=false;

  switch(getGateType(g)) {
    case BooleanGate::IN:
      return sampled.find(g)!=sampled.end();
    case BooleanGate::NOT:
      return !evaluate(*(getWires(g).begin()), sampled);
    case BooleanGate::AND:
      disjunction = false;
      break;
    case BooleanGate::OR:
      disjunction = true;
      break;
    case BooleanGate::UNDETERMINED:
      throw CircuitException("Incorrect gate type");
  }

  for(auto s: getWires(g)) {
    bool e = evaluate(s, sampled);
    if(disjunction && e)
      return true;
    if(!disjunction && !e)
      return false;
  }

  if(disjunction)
    return false;
  else
    return true;
}

double BooleanCircuit::monteCarlo(gate_t g, unsigned samples) const
{
  auto success{0u};

  for(unsigned i=0; i<samples; ++i) {
    std::unordered_set<gate_t> sampled;
    for(auto in: inputs) {
      if(rand() *1. / RAND_MAX < getProb(in)) {
        sampled.insert(in);
      }
    }

    if(evaluate(g, sampled))
      ++success;
    
    if(provsql_interrupted)
      throw CircuitException("Interrupted after "+std::to_string(i+1)+" samples");
  }

  return success*1./samples;
}

double BooleanCircuit::possibleWorlds(gate_t g) const
{ 
  if(inputs.size()>=8*sizeof(unsigned long long))
    throw CircuitException("Too many possible worlds to iterate over");

  unsigned long long nb=(1<<inputs.size());
  double totalp=0.;

  for(unsigned long long i=0; i < nb; ++i) {
    std::unordered_set<gate_t> s;
    double p = 1;

    unsigned j=0;
    for(gate_t in : inputs) {
      if(i & (1 << j)) {
        s.insert(in);
        p*=getProb(in);
      } else {
        p*=1-getProb(in);
      }
      ++j;
    }

    if(evaluate(g, s))
      totalp+=p;
   
    if(provsql_interrupted)
      throw CircuitException("Interrupted");
  }

  return totalp;
}

std::string BooleanCircuit::Save_Tseytin(gate_t g, std::string filename) const {
  std::vector<std::vector<int>> clauses;
  
  // Tseytin transformation
  for(gate_t i{0}; i<gates.size(); ++i) {
    switch(getGateType(i)) {
      case BooleanGate::AND:
        {
          int id{static_cast<int>(i)+1};
          std::vector<int> c = {id};
          for(auto s: getWires(i)) {
            clauses.push_back({-id, static_cast<int>(s)+1});
            c.push_back(-static_cast<int>(s)-1);
          }
          clauses.push_back(c);
          break;
        }

      case BooleanGate::OR:
        {
          int id{static_cast<int>(i)+1};
          std::vector<int> c = {-id};
          for(auto s: getWires(i)) {
            clauses.push_back({id, -static_cast<int>(s)-1});
            c.push_back(static_cast<int>(s)+1);
          }
          clauses.push_back(c);
        }
        break;

      case BooleanGate::NOT:
        {
          int id=static_cast<int>(i)+1;
          auto s=*getWires(i).begin();
          clauses.push_back({-id,-static_cast<int>(s)-1});
          clauses.push_back({id,static_cast<int>(s)+1});
          break;
        }

      case BooleanGate::IN:
      case BooleanGate::UNDETERMINED:
        ;
    }
  }
  clauses.push_back({(int)g+1});

  int fd;
  char cfilename[filename.length() + 8];
  strcpy(cfilename, filename.c_str());
  cfilename[filename.length()] = '_';
  cfilename[filename.length() + 1] = 'X';
  cfilename[filename.length() + 2] = 'X';
  cfilename[filename.length() + 3] = 'X';
  cfilename[filename.length() + 4] = 'X';
  cfilename[filename.length() + 5] = 'X';
  cfilename[filename.length() + 6] = 'X';
  cfilename[filename.length() + 7] = '\0';
  fd = mkstemp(cfilename);
  close(fd);

  filename=cfilename;

  std::ofstream ofs(filename.c_str());

  ofs << "p cnf " << gates.size() << " " << clauses.size() << "\n";

  for(unsigned i=0;i<clauses.size();++i) {
    for(int x : clauses[i]) {
      ofs << x << " ";
    }
    ofs << "0\n";
  }
//  if(display_prob) {
//    for(gate_t in: inputs) {
//      ofs << "w " << (static_cast<std::underlying_type<gate_t>::type>(in)+1) << " " << getProb(in) << "\n";
//      ofs << "w -" << (static_cast<std::underlying_type<gate_t>::type>(in)+1) << " " << (1. - getProb(in)) << "\n";
//    }
//  }

  ofs.close();

  return filename;
}

std::string BooleanCircuit::Tseytin(gate_t g, bool display_prob=false) const {
  std::vector<std::vector<int>> clauses;
  
  // Tseytin transformation
  for(gate_t i{0}; i<gates.size(); ++i) {
    switch(getGateType(i)) {
      case BooleanGate::AND:
        {
          int id{static_cast<int>(i)+1};
          std::vector<int> c = {id};
          for(auto s: getWires(i)) {
            clauses.push_back({-id, static_cast<int>(s)+1});
            c.push_back(-static_cast<int>(s)-1);
          }
          clauses.push_back(c);
          break;
        }

      case BooleanGate::OR:
        {
          int id{static_cast<int>(i)+1};
          std::vector<int> c = {-id};
          for(auto s: getWires(i)) {
            clauses.push_back({id, -static_cast<int>(s)-1});
            c.push_back(static_cast<int>(s)+1);
          }
          clauses.push_back(c);
        }
        break;

      case BooleanGate::NOT:
        {
          int id=static_cast<int>(i)+1;
          auto s=*getWires(i).begin();
          clauses.push_back({-id,-static_cast<int>(s)-1});
          clauses.push_back({id,static_cast<int>(s)+1});
          break;
        }

      case BooleanGate::IN:
      case BooleanGate::UNDETERMINED:
        ;
    }
  }
  clauses.push_back({(int)g+1});

  int fd;
  char cfilename[] = "/tmp/provsqlXXXXXX";
  fd = mkstemp(cfilename);
  close(fd);

  std::string filename=cfilename;
  std::ofstream ofs(filename.c_str());

  ofs << "p cnf " << gates.size() << " " << clauses.size() << "\n";

  for(unsigned i=0;i<clauses.size();++i) {
    for(int x : clauses[i]) {
      ofs << x << " ";
    }
    ofs << "0\n";
  }
  if(display_prob) {
    for(gate_t in: inputs) {
      ofs << "w " << (static_cast<std::underlying_type<gate_t>::type>(in)+1) << " " << getProb(in) << "\n";
      ofs << "w -" << (static_cast<std::underlying_type<gate_t>::type>(in)+1) << " " << (1. - getProb(in)) << "\n";
    }
  }

  ofs.close();

  return filename;
}

double BooleanCircuit::compilation(gate_t g, std::string compiler) const {
  std::string filename=BooleanCircuit::Tseytin(g);
  std::string outfilename=filename+".nnf";

  std::string cmdline=compiler+" ";
  if(compiler=="d4") {
    cmdline+=filename+" -out="+outfilename;
  } else if(compiler=="c2d") {
    cmdline+="-in "+filename+" -silent";
  } else if(compiler=="minic2d") {
    cmdline+="-in "+filename;
  } else if(compiler=="dsharp") {
    cmdline+="-q -Fnnf "+outfilename+" "+filename;
  } else {
    throw CircuitException("Unknown compiler '"+compiler+"'");
  }

  int retvalue=system(cmdline.c_str());

  if(unlink(filename.c_str())) {
    throw CircuitException("Error removing "+filename);
  }

  if(retvalue)    
    throw CircuitException("Error executing "+compiler);
  
  std::ifstream ifs(outfilename.c_str());

  std::string nnf;
  getline(ifs, nnf, ' ');

  if(nnf!="nnf") // unsatisfiable formula
    return 0.;

  unsigned nb_nodes, foobar, nb_variables;
  ifs >> nb_nodes >> foobar >> nb_variables;

  dDNNF dnnf;

  if(nb_variables!=gates.size())
    throw CircuitException("Unreadable d-DNNF (wrong number of variables: " + std::to_string(nb_variables) +" vs " + std::to_string(gates.size()) + ")");

  std::string line;
  getline(ifs,line);
  unsigned i=0;
  while(getline(ifs,line)) {
    std::stringstream ss(line);
    
    char c;
    ss >> c;

    if(c=='O') {
      int var, args;
      ss >> var >> args;
      auto id=dnnf.getGate(std::to_string(i));
      dnnf.setGate(std::to_string(i), BooleanGate::OR);
      int g;
      while(ss >> g) {
        auto id2=dnnf.getGate(std::to_string(g));
        dnnf.addWire(id,id2);
      }
    } else if(c=='A') {
      int args;
      ss >> args;
      auto id=dnnf.getGate(std::to_string(i));
      dnnf.setGate(std::to_string(i), BooleanGate::AND);
      int g;
      while(ss >> g) {
        auto id2=dnnf.getGate(std::to_string(g));
        dnnf.addWire(id,id2);
      }
    } else if(c=='L') {
      int leaf;
      ss >> leaf;
      if(gates[abs(leaf)-1]==BooleanGate::IN) {
        if(leaf<0) {
          dnnf.setGate(std::to_string(i), BooleanGate::IN, 1-prob[-leaf-1]);
        } else {
          dnnf.setGate(std::to_string(i), BooleanGate::IN, prob[leaf-1]);
        }
      } else
        dnnf.setGate(std::to_string(i), BooleanGate::IN, 1.);
    } else 
      throw CircuitException(std::string("Unreadable d-DNNF (unknown node type: ")+c+")");

    ++i;
  }

  ifs.close();
// TODO NAVE - uncomment?
//  if(unlink(outfilename.c_str())) {
//    throw CircuitException("Error removing "+outfilename);
//  }

  return dnnf.dDNNFEvaluation(dnnf.getGate(std::to_string(i-1)));
}

double BooleanCircuit::WeightMC(gate_t g, std::string opt) const {
  std::string filename=BooleanCircuit::Tseytin(g, true);

  //opt of the form 'delta;epsilon'
  std::stringstream ssopt(opt); 
  std::string delta_s, epsilon_s;
  getline(ssopt, delta_s, ';');
  getline(ssopt, epsilon_s, ';');

  double delta = 0;
  try { 
    delta=stod(delta_s); 
  } catch (std::invalid_argument &e) {
    delta=0;
  }
  double epsilon = 0;
  try {
    epsilon=stod(epsilon_s);
  } catch (std::invalid_argument &e) {
    epsilon=0;
  }
  if(delta == 0) delta=0.2;
  if(epsilon == 0) epsilon=0.8;

  //TODO calcul numIterations

  //calcul pivotAC
  const double pivotAC=2*ceil(exp(3./2)*(1+1/epsilon)*(1+1/epsilon));

  std::string cmdline="weightmc --startIteration=0 --gaussuntil=400 --verbosity=0 --pivotAC="+std::to_string(pivotAC)+ " "+filename+" > "+filename+".out";

  int retvalue=system(cmdline.c_str());
  if(retvalue) {
    throw CircuitException("Error executing weightmc");
  }

  //parsing
  std::ifstream ifs((filename+".out").c_str());
  std::string line, prev_line;
  while(getline(ifs,line))
    prev_line=line;

  std::stringstream ss(prev_line);
  std::string result;
  ss >> result >> result >> result >> result >> result;
  
  std::istringstream iss(result);
  std::string val, exp;
  getline(iss, val, 'x');
  getline(iss, exp);
  double value=stod(val);
  exp=exp.substr(2);
  double exponent=stod(exp);
  double ret=value*(pow(2.0,exponent));

  if(unlink(filename.c_str())) {
    throw CircuitException("Error removing "+filename);
  }

  if(unlink((filename+".out").c_str())) {
    throw CircuitException("Error removing "+filename+".out");
  }

  return ret;
}

double BooleanCircuit::independentEvaluationInternal(
    gate_t g, std::set<gate_t> &seen) const
{
  double result=1.;

  switch(getGateType(g)) {
    case BooleanGate::AND:
      for(const auto &c: getWires(g)) {
        result*=independentEvaluationInternal(c, seen);
      }
      break;

    case BooleanGate::OR:
      for(const auto &c: getWires(g)) {
        result*=1-independentEvaluationInternal(c, seen);
      }
      result=1-result;
      break;

    case BooleanGate::NOT:
      result=1-independentEvaluationInternal(*getWires(g).begin(), seen);
      break;

    case BooleanGate::IN:
      if(seen.find(g)!=seen.end())
        throw CircuitException("Not an independent circuit");
      seen.insert(g);
      result=getProb(g);
      break;

    case BooleanGate::UNDETERMINED:
      throw CircuitException("Bad gate");
  }

  return result;
}

double BooleanCircuit::independentEvaluation(gate_t g) const
{
  std::set<gate_t> seen;
  return independentEvaluationInternal(g, seen);
}
