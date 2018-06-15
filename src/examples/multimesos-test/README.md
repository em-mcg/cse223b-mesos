# Scalability test for Multi Master Mesos
- bg_framework.cpp is the framework provide the background traffic
- measure_framework.cpp is the framework used for measure, the result is output to stdout

Everything should be launched in such sequence:
- Build: ./build.sh
- Launch Background frameworks: ./run_background.sh
  (The default setting is launching 16 framework in background, change it in script if needed)
- Launch Measure framework: ./run_measure.sh
- Kill Background frameworks: ./kill_background.sh
