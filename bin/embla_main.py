#! /usr/bin/env python

"""Main embla run script."""

import optparse
import os
import shutil
import sys

class Unbuffered:
  def __init__(self, stream):
      self.stream = stream
  def write(self, data):
      self.stream.write(data)
      self.stream.flush()
  def __getattr__(self, attr):
      return getattr(self.stream, attr)

sys.stdout = Unbuffered(sys.stdout)
sys.stderr = Unbuffered(sys.stderr)

def build(srcDir, buildDir, installDir, options):
  if not os.path.isfile(srcDir + "/configure"):
    print "cd " + srcDir
    os.chdir(srcDir)
    autogenCommand = "./autogen.sh"
    print autogenCommand
    autogenResult = os.system(autogenCommand)
    if autogenResult:
      return 1

  if srcDir != buildDir and os.path.isfile(srcDir + "/Makefile"):
    print "cd " + srcDir
    os.chdir(srcDir)
    distcleanCommand = "make distclean"
    print distcleanCommand
    distcleanResult = os.system(distcleanCommand)
    if distcleanResult:
      return 1
    
  if not os.path.isdir(buildDir):
    print "mkdir " + buildDir
    os.mkdir(buildDir)
  print "cd " + buildDir
  os.chdir(buildDir)
  if not os.path.isfile("Makefile"):
    configureCommand = srcDir + "/configure --prefix=" + \
                       installDir + " --enable-maintainer-mode"
    print configureCommand
    configureResult = os.system(configureCommand)
    if configureResult:
      return 1

  makeCommand = "make -j " + str(options.jobs)
  print makeCommand
  makeResult = os.system(makeCommand)
  if makeResult:
    return 1

  installCommand = "make install"
  print installCommand
  installResult = os.system(installCommand)
  if installResult:
    return 1

  checkCommand = makeCommand + " check"
  print checkCommand
  checkResult = os.system(checkCommand)
  
  return checkResult != 0
  

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
  buildRoot = os.path.realpath(os.getcwd())
  installRoot = os.path.realpath("install_dir")

  srcDir = srcRoot + "/valgrind"
  buildDir = buildRoot + "/build"
  installDir = buildRoot + "/install_dir"

  ret = build(srcDir, buildDir, installDir, options)
  if ret:
    print "Build in separate directory failed"
    return ret

  # The valgrind regression tests only work when srcDir == buildDir.
  srcCopyDir = buildRoot + "/src_build"
  if not os.path.isdir(srcCopyDir):
    print "cp -r " + srcDir + " " + srcCopyDir
    shutil.copytree(srcDir, srcCopyDir)
  ret = build(srcCopyDir, srcCopyDir, buildRoot + "/install_dir_2", options)
  if ret:
    print "Build in source directory failed"
    return ret

  os.chdir(srcCopyDir)
  print "cd " + os.getcwd()

  emblaTestCOmmand = "perl tests/vg_regtest embla"
  print emblaTestCOmmand
  testResult = os.system(emblaTestCOmmand)
  if testResult:
    print "Embla regression tests failed"

  return testResult != 0
  

if __name__ == "__main__":
  sys.exit(main(sys.argv))
  
