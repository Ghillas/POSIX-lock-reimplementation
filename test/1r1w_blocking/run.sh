#! /usr/bin/env bash

rm output 2> /dev/null

OUTPUT="output"
EXPECTED="output.expected"
NAME=$(basename $(pwd))

make

if [ ! $? -eq 0 ]; then 
    return $?
fi

run_main_1() {
    ./writer 
    echo "$?" >> $OUTPUT
}

run_main_2() {
    ./reader 
    echo "$?" >> $OUTPUT
}

run_main_1 &
run_main_2 &

for job in `jobs -p`
do
    wait $jobs
done

DETAIL=$(diff -q $OUTPUT $EXPECTED)

if [ $? -eq 0 ]
then
    echo -e "\033[32m[OK] $NAME Success\033[0m"
else
    echo -e "\033[31;1;4m[KO] $NAME $DETAIL\033[0m"
fi