#! /bin/bash

kill $(ps aux | grep bg-framework | awk '{print $2}')
