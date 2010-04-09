#!/usr/bin/perl

use Getopt::Long;

my ($sraw, $swar, $swaw, $sctl, $track_hidden, $track_stack_name_deps);

GetOptions( 'sraw' => \$sraw,
            'swar' => \$swar,
            'swaw' => \$swaw,
            'sctl' => \$sctl,
            'track-hidden' => \$track_hidden,
            'track-stack-name-deps' => \$track_stack_name_deps );

$depfile = $ARGV[0];
open FH, $depfile or die $!;

my ($file, $fn, $deptype, $tgt, $src, $line);
my (%deps);

while (defined($line = <FH>)) {
  chomp($line);
  ($file, $fn, $deptype, $tgt, $src) = split (/\t/, $line);
  if ($track_hidden || $deptype !~ /H/) {
    if ($sraw && $deptype =~ /T/) {
      $deps{$file}{$src}{$tgt} = 1;
    } elsif (($swar && $deptype =~ /A/) || ($swaw && $deptype =~ /O/)) {
      if ($track_stack_name_deps || $deptype !~ /S/) {
        $deps{$file}{$src}{$tgt} = 1;
      }
    } elsif ($sctl && $deptype =~/C/) {
      $deps{$file}{$src}{$tgt} = 1;
    }
  }
}

my ($loopnum) = 0;
my ($id, $pid, $header, $restOfLine);
my (%loops);

while (defined($line = <STDIN>)) {
  chomp($line);
  ($file, $fn, $id, $pid, $header, $restOfLine) = split(/\t/, $line, 6);
  $loopnum++;
  if ($deps{$file}{-$loopnum}{$header} != 1 &&
      $deps{$file}{-$loopnum}{-$loopnum} != 1) {
    # Loop is parallel
    $loops{$file}{$id}{para} = 1;
    my ($currid) = $id;
    while ($pid != $currid && $loops{$file}{$pid}{para} != 1) {
      $currid = $pid;
      $pid = $loops{$file}{$pid}{pid};
    } 
    if ($pid == $currid) {
      # No enclosing loop
      $pid = $id;
    } 
    printf "%s\t%s\t%d\t%d\t%d\t%s\n", $file, $fn, $id, $pid, $header, $restOfLine;
  } else {
    $loops{$file}{$id}{para} = 0;
  }
  $loops{$file}{$id}{pid} = $pid;
}

