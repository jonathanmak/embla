#!/usr/bin/perl

my ($MAX_LOOPS) = 10000;

use Getopt::Long;

my $filefilter, $linefilter;

GetOptions( 'file=s' => \$filefilter,
            'line=s' => \$linefilter);

$filename = $ARGV[0];
open FH, $filename or die $!;

my ($callerfile, $callerline, $calleefile, $length, $lastline);
my (%cp);
my ($cpinfo);

while (defined($line = <FH>)) {
  if ($line =~ /^!?([-_A-Za-z\.]+):(-?[0-9]+)\(([-_A-Za-z\.]+)\)=([0-9]+): (.*)/) {
    $callerfile = $1;
    $callerline = $2;
    $calleefile = $3;
    $length = $4;
    $line = $5;
    $lastline = "";
    $cpinfo{$callerfile}{$callerline}{calleefile} = $calleefile;
    $cpinfo{$callerfile}{$callerline}{leng}{instances} += 1;
    $cpinfo{$callerfile}{$callerline}{leng}{acccost} += $length;
  }
  if ($line =~ /^(-?[0-9]+\([0-9]+\), )+$/) {
    if ($callerfile &&
        (!$filefilter || $callerfile eq $filefilter) &&
        (!$linefilter || $callerline == $linefilter)) {
      chomp($line);
      my @nodes = split /\s+/, $line;
      for $node (@nodes) {
        my @fields = split /[\(\)]/, $node;
        my $lineno = $fields[0];
        my $cost = $fields[1];
        $cp{$callerfile}{$callerline}{$lineno}{instances} += 1;
        $cp{$callerfile}{$callerline}{$lineno}{acccost} += $cost;
        if ($lastline) {
          $cp{$callerfile}{$callerline}{$lineno}{succs}{$lastline} += 1;
        }
        $lastline = $lineno;
      }
    }
  } else {
    $callerfile = $callerline = $calleefile = $length = $lastline = "";
  }
}

for $callerfile (keys %cp) {
  for $callerline (keys %{ $cp{$callerfile}}) {
    my($callerlinetext);
    if ($callerline > 0) {
      $callerlinetext = $callerline;
    } elsif ($callerline > -$MAX_LOOPS) {
      my $loopno = -$callerline;
      $callerlinetext = "LoopBody$loopno";
    } else {
      my $loopno = -( $callerline + $MAX_LOOPS );
      $callerlinetext = "Loop$loopno";
    }
    open DOT, ">$callerfile.$callerlinetext.dot" or die $!;
    $calleefile = $cpinfo{$callerfile}{$callerline}{calleefile};
    my $acclength = $cpinfo{$callerfile}{$callerline}{leng}{acccost};
    my $instlength = $cpinfo{$callerfile}{$callerline}{leng}{instances};
    my $avglength = $acclength / $instlength;
    printf DOT "digraph \"%s:%d\" {\n", $callerfile, $callerline;
    printf DOT "  label = \"%s:%s - Callee file = %s, Length = %d\";\n", $callerfile, $callerlinetext, $calleefile, $avglength;
    printf DOT "  labelloc=t;\n";
    for $lineno (keys %{ $cp{$callerfile}{$callerline}}) {
      my $instances = $cp{$callerfile}{$callerline}{$lineno}{instances};
      my $acccost = $cp{$callerfile}{$callerline}{$lineno}{acccost};
      my $avgcost = $acccost / $instances;
      if ($lineno > 0) {
        printf DOT "  %d [label = \"Line %d\\nCost:%d\"];\n", $lineno, $lineno, $avgcost;
      } elsif ($lineno > -$MAX_LOOPS) { # Loop body
        printf DOT "  %d [label = \"Body of Loop %d\\nCost:%d\"];\n", $lineno, -$lineno, $avgcost;
      } else {
        printf DOT "  %d [label = \"Loop %d\\nCost:%d\"];\n", $lineno, -($lineno+$MAX_LOOPS), $avgcost;
      }
      for $succline (keys %{ $cp{$callerfile}{$callerline}{$lineno}{succs}}) {
        my $depinsts = $cp{$callerfile}{$callerline}{$lineno}{succs}{$succline};
        printf DOT "  %d -> %d [label = \"%dx\"];\n", $lineno, $succline, $depinsts;
      }
    }
    printf DOT "}\n";
  }
}

