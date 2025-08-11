all: Master MMU Process Scheduler

Master: master.c
	gcc master.c -o Master

MMU: mmu.c
	gcc mmu.c -o MMU

Process: process.c
	gcc process.c -o Process

Scheduler: sched.c
	gcc sched.c -o Scheduler

clean:
	rm -f Master MMU Process Scheduler results.txt