#! /usr/bin/env python

"""Main embla run script."""

import optparse
import os
import sys

def main(argv):
  make_option = optparse.make_option
  parser = optparse.OptionParser(
    "usage: " + os.path.basename(argv[0]) + " [options]",
    option_list = [
    make_option("-j", "--jobs", action = "store", type = "int", default = 1,
                metavar = "n", help = "Run n parallel make jobs"),
    ])
  (options, arguments) = parser.parse_args(argv)
  if len(arguments) > 1:
    parser.print_help()
    return 1
  
  srcRoot = os.path.realpath(os.path.abspath(os.path.join(
    os.path.dirname(argv[0]), "..")))
  # XXX: Building under srcdir seems to be required for valgrind.
  buildRoot = srcRoot 
  # buildRoot = os.path.realpath(os.getcwd())
  installRoot = os.path.realpath("install_dir")
  
  print "cd " + srcRoot + "/valgrind"
  os.chdir(srcRoot + "/valgrind")
  if not os.path.isfile("Makefile"):
    configureCommand = srcRoot + "/valgrind/configure --prefix=" + \
                       installRoot
    print configureCommand
    configureResult = os.system(configureCommand)
    if configureResult:
      return configureResult

  makeCommand = "make -j " + str(options.jobs)
  print makeCommand
  makeResult = os.system(makeCommand)
  return makeResult
  
if __name__ == "__main__":
  sys.exit(main(sys.argv))
  
