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
  if not os.path.isfile("configure"):
    autogenCommand = "./autogen.sh"
    print autogenCommand
    autogenResult = os.system(autogenCommand)
    if autogenResult:
      return 1
  print "cd " + buildRoot
  os.chdir(buildRoot)
  if not os.path.isdir("valgrind"):
    print "mkdir valgrind"
    os.mkdir("valgrind")
  os.chdir("valgrind")
  print "cd " + os.getcwd()
  if not os.path.isfile("Makefile"):
    configureCommand = srcRoot + "/valgrind/configure --prefix=" + \
                       installRoot
    print configureCommand
    configureResult = os.system(configureCommand)
    if configureResult:
      return 1

  makeCommand = "make -j " + str(options.jobs)
  print makeCommand
  makeResult = os.system(makeCommand)
  if makeResult:
    return 1

  checkCommand = makeCommand + " check"
  print checkCommand
  checkResult = os.system(checkCommand)
  
  return checkResult != 0

if __name__ == "__main__":
  sys.exit(main(sys.argv))
  
