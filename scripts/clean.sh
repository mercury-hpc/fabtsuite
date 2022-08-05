#!/bin/bash
DIR=`ls *.out | cut -d'-' -f2 | cut -d'.' -f1`
mkdir ../data/$DIR
mv /ccs/proj/csc332/*.txt ../data/$DIR
mv *.out ../data/

