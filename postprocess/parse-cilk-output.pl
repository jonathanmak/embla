#!/usr/bin/perl

$progname = $ARGV[0];
$date = $ARGV[1];

my(@results) = ();

while (defined($line = <STDIN>)) {
  if ($line =~ /^Parallelism = /) {
    $args = $line;
    $args =~ s/^Parallelism = //;
    chomp($args);
    push(@results, $args);
  }
}

my $sum = 0.0;

printf "%s:\n", $progname;
foreach $i (@results) {
  $sum += $i;
  printf "%f, ", $i;
}
printf "\n";

my $n = @results;
my $mean = $sum / $n;

my $sumdiffsq = 0.0;
foreach $i (@results) {
  my $diff = $i - $mean;
  $sumdiffsq += ($diff * $diff);
}

my $variance = $sumdiffsq / $n;
my $std = sqrt($variance);

printf "%s: %u samples, mean = %f, std = %f\n", $progname, $n, $mean, $std;
printf "%s\tCilk run\t\t\t\t\t\t%s\t\t\t%f\n", $progname, $date, $mean;
