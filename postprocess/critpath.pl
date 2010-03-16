#!/usr/bin/perl

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
  if ($line =~ /^!?([-A-Za-z\.]+):(-?[0-9]+)\(([-A-Za-z\.]+)\)=([0-9]+): (.*)/) {
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
    open DOT, ">$callerfile.$callerline.dot" or die $!;
    $calleefile = $cpinfo{$callerfile}{$callerline}{calleefile};
    my $acclength = $cpinfo{$callerfile}{$callerline}{leng}{acccost};
    my $instlength = $cpinfo{$callerfile}{$callerline}{leng}{instances};
    my $avglength = $acclength / $instlength;
    printf DOT "// %s:%d - Callee file = %s, Length = %d \n", $callerfile, $callerline, $calleefile, $avglength;
    printf DOT "digraph \"%s:%d\" {\n", $callerfile, $callerline;
    for $lineno (keys %{ $cp{$callerfile}{$callerline}}) {
      my $instances = $cp{$callerfile}{$callerline}{$lineno}{instances};
      my $acccost = $cp{$callerfile}{$callerline}{$lineno}{acccost};
      my $avgcost = $acccost / $instances;
      printf DOT "  %d [label = \"%d:%d\"];\n", $lineno, $lineno, $avgcost;
      for $succline (keys %{ $cp{$callerfile}{$callerline}{$lineno}{succs}}) {
        my $depinsts = $cp{$callerfile}{$callerline}{$lineno}{succs}{$succline};
        printf DOT "  %d -> %d [label = \"%d\"];\n", $lineno, $succline, $depinsts;
      }
    }
    printf DOT "}\n";
  }
}

