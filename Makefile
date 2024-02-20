all:
	gcc -O3 two.cc -luring -fpermissive

ABT_PATH=/home/tomoya-s/work/github/ppopp21-preemption-artifact/argobots/install

arg:
	gcc -O3 -g argobots.cc -I $(ABT_PATH)/include -L $(ABT_PATH)/lib -labt -lopcodes -luring
