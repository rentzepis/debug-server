#!/usr/bin/perl -w
#
# port-for-user.pl - Return a port number, p, for a given user, with a
#    low probability of collisions. The port p is always even, so that
#    users can use p and p+1 for testing with proxy and the tiny web
#    server.
#
#    usage: ./port-for-user.pl [optional user name(s)]
#
#    With no argument, hashes the student's Andrew ID. Every student
#    container runs as the same Unix user (coder), so the Andrew ID is
#    taken from CODE_SERVER_USERNAME, set by create_codeserver.sh.
#
use strict;
use Digest::MD5;

my $maxport = 65536;
my $minport = 1024;

# hashname - compute an even port number from a hash of the argument
sub hashname {
    my $name = shift;
    my $port;
    my $hash = Digest::MD5::md5_hex($name);
    # take only the last 32 bits => last 8 hex digits
    $hash = substr($hash, -8);
    $hash = hex($hash);
    $port = $hash % ($maxport - $minport) + $minport;
    $port = $port & 0xfffffffe;
    print "$name: $port\n";
}

# If called with no argument, then hash the Andrew ID (falling back to
# the Unix user name), otherwise hash the command line argument(s).
if ($#ARGV == -1) {
    my $username = $ENV{'CODE_SERVER_USERNAME'};
    if (!defined($username) || $username eq '') {
        ($username) = getpwuid($<);
    }
    hashname($username);
} else {
    foreach (@ARGV) {
        hashname($_);
    }
}
