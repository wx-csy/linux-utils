# linux-utils
some Linux utilities

pstree
-------

    Usage: pstree [ -n ] [ -p ]
           pstree -V
    Display a tree of processes.

      -n, --numeric-sort  sort output by PID
      -p, --show-pids     show PIDs
      -V, --version       display version information
      
perf
-----
Print time consumption of systems calls.

    Usage: perf command [arg]...

crepl
------
A simple read-eval-write loop for C programming language. Use gcc as compiler.

- A line beginning with "int " is considered as a function that returns `int`;
- Otherwise it is considered as an expression of type `int`;
- Any invalid input will lead to undefined behaviour.

### Example ###
    
    This is a read-eval-print loop for C programming language.
    To exit, type `exit'.
    >>> int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
    >>> gcd(256, 144) * gcd(56, 84)
    448
    >>> exit
    
