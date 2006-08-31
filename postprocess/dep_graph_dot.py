#! /usr/bin/env python

import optparse
import sys

arrowSize = .5
fontName = "Courier"
fontSize = 8

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

  dotLines = ["digraph " + graphName + " {",
              '  fontname = "' + fontName + '";',
              '  fontsize = ' + str(fontSize) + ';',
              "  nodesep = .2;",
              "  ranksep = 0;"]

  nodeLabels = {}
  for dep in deps:
    for (end, depth) in [dep.dest, dep.src]:
      if depth != 'h':
        nodeLabels[end] = min(nodeLabels.get(end, 'l'), depth)

  dotLines.extend(["  {",
                   '    node [fontname = "' + fontName +
                   '", fontsize = ' + str(fontSize) + '];'])
  for lineNum in nodeLabels.keys():
    dotLines.append("    end_" + str(lineNum) + ' [shape = point];')
  dotLines.extend(["  }",
                   ""])

  for dep in deps:
    if (dep.src[1] != 'h') and (dep.dest[1] != 'h'):
      dotLines.append("  end_" + str(dep.src[0]) + " -> end_" +
                      str(dep.dest[0]) + ' [arrowsize = ' + str(arrowSize) +
                      '];')

  dotLines.extend(["  subgraph {",
                   '    node [shape = plaintext, fontname = "' + fontName +
                   '", fontsize = ' + str(fontSize) + '];',
                   "    edge [style = invis];"])

  # Only print the source lines for this function.
  nodeLines = nodeLabels.keys()
  nodeLines.sort()
  debug(4, "Node lines: " + str(nodeLines))
  # Include the two line before the first (usually return type and function
  # name).
  firstLine = max(1, nodeLines[0] - 2)
  lastLine = nodeLines[-1]
  sourceLineLength = 0
  for srcLine in sourceLines[firstLine - 1: lastLine - 1]:
    sourceLineLength = max(sourceLineLength, len(srcLine))

  for lineIndex in range(firstLine, lastLine + 1):
    sourceLine = sourceLines[lineIndex - 1].rstrip()
    dotLines.append("    line_" + str(lineIndex) + ' [label = "' +
                    str(lineIndex) + "  " +
                    sourceLine.replace('\\', '\\\\').replace('"', '\\"') +
                    (" " * (sourceLineLength - len(sourceLine))) + '"];')
    if lineIndex != firstLine:
      dotLines.append("    line_" + str(lineIndex - 1) + " -> line_" +
                      str(lineIndex) + ';')
  dotLines.extend(["  }",
                   ""])

  for lineNum in nodeLabels.keys():
    dotLines.append("  { rank = same; end_" + str(lineNum) + "; line_" +
                    str(lineNum) + "; }")
  dotLines.append("")

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