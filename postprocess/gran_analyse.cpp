#include "gsl/gsl_matrix.h"
#include "gsl/gsl_vector.h"
#include "gsl/gsl_linalg.h"
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <list>
#include <set>

using namespace std;

int sraw = 0;
int swar = 0;
int swaw = 0;
int sctl = 0;
int track_hidden = 0;
int track_stack_name_deps = 0;
string lengths_file;
string edge_file;
string dep_file;

class Stats {
  public:
    unsigned long long int n;
    double mean;
    Stats() : n(0), mean(0.0) {}
};

class LineInfo; 
class LineInfo {
  public:
    string file;
    string fn;
    int line;
    Stats containsCall;
    Stats notContainsCall;
    map<LineInfo*, unsigned long long int> succs;
    set<LineInfo*> depTgts;
};

map<string, map<int, LineInfo> > table;

static LineInfo& getLineInfo(string& file, string& fn, int line) {
  if (table[file].find(line) == table[file].end()) {
    table[file][line].file = file;
    table[file][line].fn = fn;
    table[file][line].line = line;
  }
  return table[file][line];
}

void readLengths(void) {
  ifstream ifs(lengths_file.c_str());
  string file;
  string fn;
  int lineNo;
  string containsCall;
  unsigned long long int n;
  double mean;
  double variance;
  
  while (ifs.good()) {
    string temp;

    getline(ifs, file, '\t');

    getline(ifs, fn, '\t');

    getline(ifs, temp, '\t');
    lineNo = atoi(temp.c_str());

    getline(ifs, containsCall, '\t');

    getline(ifs, temp, '\t');
    n = strtoul(temp.c_str(), NULL, 10);

    getline(ifs, temp, '\t');
    mean = atof(temp.c_str());

    getline(ifs, temp, '\n');
    variance = atof(temp.c_str());
    
    if (lineNo > 0) { // Ignore loops
      Stats& stats = containsCall == "T" ?
        getLineInfo(file, fn, lineNo).containsCall :
        getLineInfo(file, fn, lineNo).notContainsCall;
      stats.n = n;
      stats.mean = mean;
    }
  }
}

void readEdges(void) {
  ifstream ifs(edge_file.c_str());
  string file;
  string fn;
  int fromLineNo;
  int toLineNo;
  unsigned long long int n;
  
  while (ifs.good()) {
    string temp;

    getline(ifs, file, '\t');

    getline(ifs, fn, '\t');

    getline(ifs, temp, '\t');
    fromLineNo = atoi(temp.c_str());

    getline(ifs, temp, '\t');
    toLineNo = atoi(temp.c_str());

    getline(ifs, temp, '\n');
    n = strtoul(temp.c_str(), NULL, 10);
    
    getLineInfo(file, fn, fromLineNo).succs[&(getLineInfo(file, fn, toLineNo))] = n;
  }
}

#define CONTAINS_STR(str, exp) ( str.find(exp) != string::npos )

void readDeps(void) {
  ifstream ifs(dep_file.c_str());
  string file;
  string fn;
  string depType;
  int tgtLineNo;
  int srcLineNo;
  
  while (ifs.good()) {
    string temp;

    getline(ifs, file, '\t');

    getline(ifs, fn, '\t');

    getline(ifs, depType, '\t');

    getline(ifs, temp, '\t');
    tgtLineNo = atoi(temp.c_str());

    getline(ifs, temp, '\n');
    srcLineNo = atoi(temp.c_str());

    if (track_hidden || !CONTAINS_STR( depType, 'H')) {
      if (sraw && CONTAINS_STR(depType, 'T')) {
        getLineInfo(file, fn, srcLineNo).depTgts.insert(&(getLineInfo(file, fn, tgtLineNo)));
      } else if ((swar && CONTAINS_STR(depType, 'A')) || (swaw && CONTAINS_STR(depType, 'O'))) {
        if (track_stack_name_deps || !CONTAINS_STR(depType, 'S')) {
          getLineInfo(file, fn, srcLineNo).depTgts.insert(&(getLineInfo(file, fn, tgtLineNo)));
        }
      } else if (sctl && CONTAINS_STR(depType, 'C')) {
        getLineInfo(file, fn, srcLineNo).depTgts.insert(&(getLineInfo(file, fn, tgtLineNo)));
      }
    }
  }
}

void findAllContinuationLines(map<int, int>& lineToIdx, LineInfo& callLine, LineInfo& predLine) {
  for (map<LineInfo*, unsigned long long int>::iterator si = predLine.succs.begin();
      si != predLine.succs.end(); si++) {
    LineInfo* succ = si->first;
    int lineNo = succ->line;
    if (lineToIdx.find(lineNo) == lineToIdx.end() &&
        callLine.depTgts.find(succ) == callLine.depTgts.end()) {
      int newIndex = lineToIdx.size();
      lineToIdx[lineNo] = newIndex;
//      cout << lineNo << "->" << newIndex << endl;
      findAllContinuationLines(lineToIdx, callLine, *succ);
    }
  }
}

void populate(
    gsl_matrix *probM,
    gsl_vector *lengthsV,
    map<int, int>& lineToIdx,
    LineInfo& callLine,
    LineInfo& lineInfo,
    set<int>& visited) {

  unsigned long long int n = lineInfo.containsCall.n + lineInfo.notContainsCall.n;
  int index = lineToIdx[lineInfo.line];
  visited.insert(lineInfo.line);

  // Work out lengths
  double length = ( ( ( (double)lineInfo.containsCall.n) * lineInfo.containsCall.mean ) +
                    (  ( (double)lineInfo.notContainsCall.n) * lineInfo.notContainsCall.mean ) ) /
                  ( (double) n );
  gsl_vector_set(lengthsV, index, length);

  // Work out probabilities
  for (map<LineInfo*, unsigned long long int>::iterator si = lineInfo.succs.begin();
      si != lineInfo.succs.end(); si++) {
    LineInfo* succ = si->first;
    int succNo = succ->line;
    if (callLine.depTgts.find(succ) == callLine.depTgts.end()) {
      unsigned long long int edgeN = si->second;
      int succIndex = lineToIdx[succNo];
      double prob = ((double) edgeN) / ((double) n);
      gsl_matrix_set(probM, index, succIndex, prob);
      if (visited.find(succNo) == visited.end()) {
        populate(probM, lengthsV, lineToIdx, callLine, *succ, visited);
      }
    }
  }
}

double calculateContinuationLength(LineInfo& lineInfo) {
  map<int, int> lineToIdx;
  lineToIdx[lineInfo.line] = 0;

  findAllContinuationLines(lineToIdx, lineInfo, lineInfo);

  int size = lineToIdx.size();

  gsl_matrix *probM = gsl_matrix_calloc(size, size);
  gsl_matrix *solveM = gsl_matrix_alloc(size, size);
  gsl_matrix_set_identity(solveM);
  gsl_vector *lengthsV = gsl_vector_calloc(size);
  gsl_permutation * p = gsl_permutation_alloc(size);

  set<int> visited;
  populate(probM, lengthsV, lineToIdx, lineInfo, lineInfo, visited);
  double callLineLength = gsl_vector_get(lengthsV, 0);
//  gsl_matrix_fprintf( stdout, probM, "%.3f" );

  gsl_matrix_sub(solveM, probM);
//  cout << endl;
//  gsl_matrix_fprintf( stdout, solveM, "%.3f" );
//  cout << endl;
//  gsl_vector_fprintf( stdout, lengthsV, "%.3f" );

  int s;
  gsl_linalg_LU_decomp(solveM, p, &s);
  gsl_linalg_LU_svx(solveM, p, lengthsV); // Solution is written back to lengthsV

//  cout << endl;
//  gsl_vector_fprintf( stdout, lengthsV, "%.3f" );

  double length = gsl_vector_get(lengthsV, 0) - callLineLength;

  gsl_permutation_free(p);
  gsl_matrix_free(probM);
  gsl_matrix_free(solveM);
  gsl_vector_free(lengthsV);

  return length;
}

void printMeanLengths(void) {
  for (map<string, map<int, LineInfo> >::iterator ti = table.begin(); ti != table.end(); ti++) {
    string file = ti->first;
    map<int, LineInfo>& lines = ti->second;
    for (map<int, LineInfo>::iterator li = lines.begin(); li != lines.end(); li++) {
      int line = li->first;
      LineInfo& lineInfo = li->second;
      if (lineInfo.containsCall.n > 0) {
        double taskLength = lineInfo.containsCall.mean;
        double continuationLength = calculateContinuationLength(lineInfo);

        printf("%s\t%s\t%d\t%.*f\t%.*f\n", file.c_str(), lineInfo.fn.c_str(), line, 0, taskLength, 0, continuationLength);
      }
    }
  }
}

#define IS_ARG(key) strcmp(argv[i], key) == 0

int main(int argc, char *argv[]) {
  int i;
  for (i=1; i<argc; i++) {
    if (IS_ARG("--sraw")) {
      sraw = 1;
    } else if (IS_ARG("--swar")) {
      swar = 1;
    } else if (IS_ARG("--swaw")) {
      swaw = 1;
    } else if (IS_ARG("--sctl")) {
      sctl = 1;
    } else if (IS_ARG("--track-hidden")) {
      track_hidden = 1;
    } else if (IS_ARG("--track-stack-name-deps")) {
      track_stack_name_deps = 1;
    } else if (IS_ARG("--lengths-file")) {
      i++;
      lengths_file = argv[i];
    } else if (IS_ARG("--edge-file")) {
      i++;
      edge_file = argv[i];
    } else if (IS_ARG("--dep-file")) {
      i++;
      dep_file = argv[i];
    } else {
      printf("Unknown option: %s\n", argv[i]);
      exit(1);
    }
  }

  if (lengths_file == "") {
    printf("--lengths-file must be specified.\n");
    exit(1);
  }

  if (edge_file == "") {
    printf("--edge-file must be specified.\n");
    exit(1);
  }

  if (dep_file == "") {
    printf("--dep-file must be specified.\n");
    exit(1);
  }

  readLengths();
  readEdges();
  readDeps();

  printMeanLengths();
}
