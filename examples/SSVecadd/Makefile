CLANG = clang
LLC = llc
RV_ROOT = $(SS_TOOLS)/riscv64-unknown-elf
INCLUDE = -I $(SS_TOOLS)/include

ifndef SSCONFIG
$(warning SSCONFIG is undefined, using default)
SSCONFIG=$(SS)"/ss-scheduler/configs/revel-1x1.sbmodel"
endif

vecadd.dfg.h: vecadd.dfg
	ss_sched -a sa --max-iters 20000 -d --verbose $(SSCONFIG) $<

main.bc: main.c vecadd.dfg.h
	$(CLANG) $< -o $@ -emit-llvm -O -c -O2 --sysroot=$(RV_ROOT) $(INCLUDE)

auto-main.bc: main.c vecadd.dfg.h
	$(CLANG) -DAUTO $< -o $@ -emit-llvm -O -c -O2 --sysroot=$(RV_ROOT) $(INCLUDE)

main.s: main.bc
	$(LLC) $<

clean:
	rm -f *.bc *.ll *.o *.exe *.s *.dfg.h dfg*.dfg
