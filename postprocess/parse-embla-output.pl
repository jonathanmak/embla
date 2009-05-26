#!/usr/bin/perl

$progname = $ARGV[0];

while (defined($line = <STDIN>)) {
  if ($line =~ /^======================================================$/) {
    # New run
    if ($args && $date && $n_instr && $c_path) {
      printf "%s\t%s\t%s\t%u\t%u\n", $progname, $args, $date, $n_instr, $c_path;
    }
    $args = $date = $n_instr = $c_path = "";
  } elsif ($line =~ /^Arguments: /) {
    $args = $line;
    $args =~ s/^Arguments: //;
    chomp($args);
  } elsif ($line =~ /^Date: /) {
    $date = $line;
    $date =~ s/^Date: //;
    chomp($date);
  } elsif ($line =~ /^==\d+== No. of instructions is \d+$/) {
    $n_instr = $line;
    $n_instr =~ s/^==\d+== No. of instructions is //;
    chomp($n_instr);
  } elsif ($line =~ /^==\d+== Length of Critical path is \d+$/) {
    $c_path = $line;
    $c_path =~ s/^==\d+== Length of Critical path is //;
    chomp($c_path);
  }
}
if ($args && $date && $n_instr && $c_path) {
  printf "%s\t%s\t%s\t%u\t%u\n", $progname, $args, $date, $n_instr, $c_path;
}
