#! /usr/bin/env python

import optparse
import sys

class Debug:
  def __init__(self, level):
    self._level = level

  def changeLevel(self, change):
    self._level += change
    self(2, "Changed debug level to " + str(self._level))

  def __call__(self, level, msg):
    if level <= self._level:
      print msg

debug = Debug(2)

def parseDepEnd(lineNum):
  if lineNum[-1].isalpha():
    return (int(lineNum[:-1]), lineNum[-1])
  else:
    return (int(lineNum), "l")

class DependencyItem:
  def __init__(self, traceLine):
    words = traceLine.split()
    debug(6, "Constructing dependency from " + repr(words))
    (self.file, self.func, self.type, dest, src) = words[:5]
    self.dest = parseDepEnd(dest)
    self.src = parseDepEnd(src)
    (self.raw, self.war, self.waw) = map(int, words[5:])

def buildDepMap(traceFile):
  debug(1, "Parsing " + traceFile)
  fh = file(traceFile)
  map = {}
  for line in fh:
    debug(3, "Parsing line " + repr(line.strip()))
    item = DependencyItem(line.strip())
    key = (item.file, item.func)
    debug(4, "Dependency key: " + str(key))
    if not key in map:
      map[key] = []
    map[key].append(item)
  fh.close()
  return map

def writeGraph(allDeps):
  deps = filter(lambda d: d.type != 'f', allDeps)
  assert(len(deps) > 0)
  sourceFileName = deps[0].file
  func = deps[0].func
  graphName = sourceFileName.replace(".", "_") + "__" + func
  dotFileName = graphName + ".dot"
  debug(2, "Creating " + dotFileName)

  sourceFile = file(sourceFileName)
  sourceLines = sourceFile.readlines()
  sourceFile.close()

  dotLines = ["digraph " + graphName + " {"]
  dotLines.extend(["  {",
                   "    node [shape = plaintext];",
                   "    edge [style = invis];"])
  for lineIndex in range(1, len(sourceLines) + 1):
    dotLines.append("    line_" + str(lineIndex) + ' [label = "' +
                    str(lineIndex + 1) + "  " +
                    sourceLines[lineIndex - 1].rstrip().replace('"', '\\"') +
                    '"];')
    if lineIndex != 1:
      dotLines.append("    line_" + str(lineIndex - 1) + " -> line_" +
                      str(lineIndex) + ';')
  dotLines.extend(["  }",
                   ""])

  nodes = {}
  for dep in deps:
    for (end, depth) in [dep.dest, dep.src]:
      if depth != 'h':
        nodes[end] = 1
          
  for lineNum in nodes.keys():
    dotLines.append("  { rank = same; end_" + str(lineNum) + "; line_" +
                    str(lineNum) + "; }")
  dotLines.append("")

  for dep in deps:
    if (dep.src[1] != 'h') and (dep.dest[1] != 'h'):
      dotLines.append("  end_" + str(dep.src[0]) + " -> end_" +
                      str(dep.dest[0]) + ';')
  dotLines.extend(["}", ""])
    
  dotFile = file(dotFileName, "w")
  dotFile.write("\n".join(dotLines + [""]))
  dotFile.close()
  

def main(argv):
  make_option = optparse.make_option
  parser = optparse.OptionParser(
    usage = "\n".join([
    "usage: %prog [options] trace_file",
    "",
    "%prog generates dot graph files from an embla trace file."]),
    option_list = [
    make_option("-q", "--quiet", action = "count", default = 0),
    make_option("-v", "--verbose", action = "count", default = 0)
    ])
  (options, args) = parser.parse_args(argv)

  debug.changeLevel(options.verbose - options.quiet)

  depMap = buildDepMap(args[1])
  for deps in depMap.values():
    writeGraph(deps)

  return 0

if __name__ == "__main__":
  sys.exit(main(sys.argv))
