#! /bin/bash

kill $(ps aux | grep real-framework | awk '{print $2}')
