#! /usr/bin/env python

import commands
import optparse
import os
import shutil
import socket
import sys
import tempfile

class Config:
  trunkUrl = "http://ungoliant.sics.se:8000/svn/trunk/embla"

class Debug:
  def __init__(self, log, verbose):
    if log:
      self._log = open(log, "w")
    else:
      self._log = sys.stdout
    self._verbose = verbose

  def write(self, msg, level = 1):
    if level >= self._verbose:
      self._log.write(msg + "\n")

def main(argv):
  make_option = optparse.make_option
  parser = optparse.OptionParser(
    usage = os.path.basename(argv[0]) +
    """ [options]""",
    option_list = [
    make_option("-d", "--debug-log", action = "store",
                help = "Store debug messages in log"),
    make_option("-t", "--tmpdir", action = "store",
                default = os.environ.get("TMPDIR", tempfile.gettempdir()),
                help = "Store temporary files in TMPDIR"),
    make_option("-v", "--verbose", action = "count", default = 0,
                help = "Print debug messages")])
  (options, arguments) = parser.parse_args(argv[1:])
  debug = Debug(options.debug_log, options.verbose).write
  baseDir = options.tmpdir
  while not os.path.isdir(baseDir):
    try:
      os.makedirs(dir, mode)
    except OSError, err:
      if err.errno != errno.EEXIST:
        raise
  
  os.chdir(baseDir)
  debug("cd " + os.getcwd())
  
  latestDir = "latest"
  lastWorkingDir = "last_working"
  yesterdayDir = "yesterday"
  logDir = os.path.join(baseDir, latestDir)

  if os.path.islink(yesterdayDir):
    debug("rm " + yesterdayDir)
    os.remove(yesterdayDir)
  elif os.path.isdir(yesterdayDir):
    debug("rm -rf " + yesterdayDir)
    shutil.rmtree(yesterdayDir)
  if os.path.isdir(latestDir):
    debug("mv " + latestDir + " " + yesterdayDir)
    os.rename(latestDir, yesterdayDir)
    
  debug("mkdir " + latestDir)
  os.mkdir(latestDir)
  os.chdir(latestDir)
  debug("cd " + os.getcwd())
  
  (svnStatus, svnOutput) = commands.getstatusoutput("svn checkout " +
                                                    Config.trunkUrl)
  if svnStatus != 0:
    debug(svnOutput, 0)
    return 1
  open(logDir + "/svn.out", "w").write(svnOutput)
  (infoStatus, infoOutput) = commands.getstatusoutput("svn info embla")
  if infoStatus != 0:
    debug(infoOutput, 0)
  revision = filter(lambda l: l.startswith("Revision:"),
                    infoOutput.splitlines())[0].split()[-1]

  debug("mkdir build")
  os.mkdir("build")
  os.chdir("build")
  debug("cd " + os.getcwd())

  debug("Building revision " + revision)

  buildCommand = "../embla/bin/embla_main.py"
  debug(buildCommand)
  (buildStatus, buildOutput) = commands.getstatusoutput(buildCommand)
  open(logDir + "/build.out", "w").write(buildOutput)

  os.chdir(baseDir)
  debug("cd " + os.getcwd())
  if buildStatus == 0:
    if os.path.isdir(lastWorkingDir):
      debug("rm -rf " + lastWorkingDir)
      shutil.rmtree(lastWorkingDir)
    debug("mv " + latestDir + " " + lastWorkingDir)
    os.rename(latestDir, lastWorkingDir)
    debug("ln -s " + lastWorkingDir + " " + latestDir)
    os.symlink(lastWorkingDir, latestDir)
    debug("\n".join([
      "Bootstrap succeeded for revision " + revision + ". Saved as",
      socket.gethostname() + ":" + os.path.join(baseDir, lastWorkingDir)]), 0)
  else:
    debug("\n".join([
      "Nightly build and run failed for revision " + revision + ".",
      "Build files are left in " + socket.gethostname() + ":" +
      os.path.join(baseDir, latestDir),
      "Output:"
      "",
      output]), 0)

  return buildStatus

if __name__ == "__main__":
  sys.exit(main(sys.argv))
