CPU=L865
KVER=2.6.24.7-rt27
KERNELPATH=/acc/sys/$(CPU)/usr/src/kernels/$(KVER)/

OUTPUTDIR=$(PWD)/$(CPU)/$(KVER)

modules:
	make -C $(KERNELPATH) M=$(PWD) modules
	mkdir -p $(OUTPUTDIR)
	mv *.o *.ko $(OUTPUTDIR)
clean:
	make -C $(KERNELPATH) M=$(PWD) clean
	rm -rf L865/
