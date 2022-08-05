# Multi-node Test on Crusher

`time -p` command was used.
The output of `real` in seconds was used for the following table.


* N = number of nodes on Crusher
* n = total number of sessions on fget.
* k = number of sessions on each fput


N | n:k |fget |fput [min, max]|
===============================
2 | 1:1 |6.34 | 6.28|
3 | 2:1 |6.85 |[6.32,6.71]|
3 | 4:2 |10.19|[9.43,10.07]|
