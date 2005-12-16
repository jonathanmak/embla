#! /usr/bin/env python

import commands
import errno
import optparse
import os
import random
import smtplib
import socket
import sys
import tempfile
import time

class Config:
  mailRecipients = ["hild@sics.se"]
  mailSender = "lalle@sics.se"
  subjectPrefix = "[Embla]"
  cronHosts = [host + ".sics.se" for host in ("saruman", "ungoliant")]
  runOnHosts = {}
  priorities = {}
  defaultPriority = 5

def main(argv):
  os.nice(15)
  make_option = optparse.make_option
  parser = optparse.OptionParser(
    usage = os.path.basename(argv[0]) +
    """ [options] (hourly|daily|weekly|monthly)""",
    option_list = [
    make_option("-c", "--cron-dir", action = "store",
                default = os.path.dirname(argv[0]),
                help = "Search for scripts under CRON_DIR"),
    make_option("-l", "--log-dir", action = "store",
                default = os.path.join(os.path.dirname(argv[0]), "logs",
                                       socket.gethostname().split(".")[0]),
                help = "Store logs under LOG_DIR"),
    make_option("-r", "--random-delay", action = "store", type = "int",
                default = 1800,
                help = "Wait between zero and RANDOM_DELAY seconds before " +
                "starting"),
    make_option("-v", "--verbose", action = "count", default = 0,
                help = "Print debug messages")])
  (options, arguments) = parser.parse_args(argv[1:])
  if len(arguments) != 1:
    parser.print_help()
    return 1
  cronDir = os.path.join(options.cron_dir, arguments[0])
  returnCode = 0

  delay = random.randint(0, options.random_delay)
  if options.verbose:
    print "Waiting " + str(delay) + " seconds before starting"
  time.sleep(delay)

  if os.path.isdir("/scratch") and not "TMPDIR" in os.environ:
    tempfile.tempdir = "/scratch"
  tmpDir = tempfile.gettempdir() + "/embla/hild"
  while not os.path.isdir(tmpDir):
    try:
      os.makedirs(tmpDir)
    except OSError, err:
      if err.errno != errno.EEXIST:
        raise

  if options.verbose:
    print "Looking for scripts in " + cronDir
    # Only send mail to whoever is debugging the script
    mailRecipients = (os.environ.get("USERNAME", os.environ["LOGNAME"]) +
                      "@sics.se", )
  else:
    mailRecipients = Config.mailRecipients
  verbosityFlag = " -v" * options.verbose
  
  if os.path.isdir(cronDir):
    cronScripts = []
    for script in os.listdir(cronDir):
      if os.path.isfile(cronDir + "/" + script) and \
         (script[-1] != "~") and (script[0] != "."):
        if socket.gethostname() in \
           Config.runOnHosts.get(script, Config.cronHosts):
          cronScripts.append((
            Config.priorities.get(
            script, Config.defaultPriority), script))
    cronScripts.sort()
    if options.verbose:
      print "Found scripts: " + str(cronScripts)
    for (prio, script) in cronScripts:
      os.environ["TMPDIR"] = tmpDir + "/" + os.path.splitext(script)[0]
      if not os.path.isdir(os.environ["TMPDIR"]):
        os.makedirs(os.environ["TMPDIR"])
      logDir = os.path.join(options.log_dir, arguments[0])
      logFile = os.path.join(logDir, os.path.splitext(script)[0] + ".log")
      if not os.path.isdir(logDir):
        if options.verbose:
          print "mkdir -p " + logDir
        os.makedirs(logDir)
      scriptCommand = cronDir + "/" + script + verbosityFlag + \
                      " --debug-log " + logFile
      if options.verbose:
        print " ".join(["PYTHONPATH=" + os.environ["PYTHONPATH"],
                        "TMPDIR=" + os.environ["TMPDIR"],
                        scriptCommand])
      (status, output) = commands.getstatusoutput(scriptCommand)
      if options.verbose:
        print "Output:\n" + output + "\n"
        print "Exit status: " + str(status)
      if status != 0 or (output.strip() != ""):
        if status != 0:
          success = "failure"
          returnCode = 1
        else:
          success = "success"
        subject = Config.subjectPrefix + " cron job " + success + ": " + script
        message = "Embla cron job " + script + " " + success + " on " + \
                  socket.gethostname() + ", output:\n"
        email = "\n".join(
          ("From: " + Config.mailSender,
           "To: " + ", ".join(mailRecipients),
           "Subject: " + subject,
           "",
           message,
           output,
           ""))

        if options.verbose:
          print "Sending email to " + ", ".join(mailRecipients) + ":\n"
          print email
        smtpConnection = smtplib.SMTP()
        smtpConnection.connect()
        smtpConnection.sendmail(Config.mailSender, mailRecipients, email)
        smtpConnection.quit()

  return returnCode
  
if __name__ == "__main__":
  sys.exit(main(sys.argv))
