#! /bin/bash
for i in {1..16}
do
  ./bg-framework --master=127.0.0.1:5050 1>tmp &
done

