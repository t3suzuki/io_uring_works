ABT_PATH=/home/tomoya-s/work/github/ppopp21-preemption-artifact/argobots/install
PTHABT_PATH=/home/tomoya-s/mountpoint2/tomoya-s/pthabt/newlib

arg:
	gcc -O3 -g argobots.cc -I $(ABT_PATH)/include -L $(ABT_PATH)/lib -labt -lopcodes -luring
#	gcc -O3 -g argobots.cc -I $(ABT_PATH)/include -L $(ABT_PATH)/lib -labt -lopcodes -L/home/tomoya-s/work/io_uring_works/liburing/install -luring


two:
	gcc -O3 two.cc -luring -fpermissive


FLAGS += -O3
FLAGS += -g
FLAGS += -I $(ABT_PATH)/include

dir:
	g++ $(FLAGS) -DND=1 -DN_CORE=3 nvme.cc -c
	gcc $(FLAGS) nvme.o arg_direct.c  -L $(ABT_PATH)/lib -labt -lopcodes -lstdc++ -luring

