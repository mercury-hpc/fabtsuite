# Multi-node Test on Crusher

`time -p` command was used.
The output of `real` in seconds was used for the following table.


* N = number of nodes on Crusher
* n = total number of sessions on fget.
* k = number of sessions on each fput

## Default

|N|n:k|fget|fput |
|-|----|----|-----|
|2|1:1|6.34|6.28 |
|3|2:1|6.85|6.32,6.71|
|3|4:2|10.19|9.43,10.07|
|6|4:2|10.97,11.58|8.36,8.69,8.97,9.58|

## Contiguos (-g)

|N|n:k|fget|fput |
|-|----|----|-----|
|6|4:2|10.39,10.82|7.30,8.19,8.41,8.79|

## Reregister (-r)

|N|n:k|fget|fput |
|-|----|----|-----|
|6|4:2|17.81,17.90|14.83,14.85,15.80,15.87|


