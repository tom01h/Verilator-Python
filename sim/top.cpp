#include "sim/Vtop.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

union int_char {
    char c[4];
    int i;
};
 
vluint64_t vcdstart = 0;
vluint64_t vcdend = vcdstart + 300000;
vluint64_t main_time;
Vtop* verilator_top;
VerilatedVcdC* tfp;

void eval()
{
    // negedge clk /////////////////////////////
    verilator_top->S_AXI_ACLK = 0;
    verilator_top->AXIS_ACLK = 0;

    verilator_top->eval();

    if((main_time>=vcdstart)&((main_time<vcdend)|(vcdend==0)))
        tfp->dump(main_time);
    main_time += 5;

    // posegedge clk /////////////////////////////
    verilator_top->S_AXI_ACLK = 1;
    verilator_top->AXIS_ACLK = 1;

    verilator_top->eval();

    if((main_time>=vcdstart)&((main_time<vcdend)|(vcdend==0)))
        tfp->dump(main_time);
    main_time += 5;

    return;
}

void v_init() {
    verilator_top->S_AXI_ARESETN = 0;
    verilator_top->AXIS_ARESETN = 0;
    verilator_top->S_AXI_BREADY  = 1;
    verilator_top->S_AXI_WSTRB   = 0xf;
    verilator_top->S_AXI_RREADY  = 1;
    verilator_top->S_AXIS_TSTRB  = 0xf;
    verilator_top->S_AXIS_TLAST  = 0;
    verilator_top->M_AXIS_TREADY = 1;
    verilator_top->S_AXI_ARVALID = 0;
    verilator_top->S_AXI_AWVALID = 0;
    verilator_top->S_AXI_WVALID  = 0;
    verilator_top->S_AXIS_TVALID = 0;

    for(int i=0; i<10; i++) eval();
    verilator_top->S_AXI_ARESETN = 1;
    verilator_top->AXIS_ARESETN = 1;
}

void v_finish() {
    for(int i=0; i<10; i++) eval();
}

void v_write(int address, int data){
    verilator_top->S_AXI_AWADDR  = address;
    verilator_top->S_AXI_WDATA   = data;
    verilator_top->S_AXI_AWVALID = 1;
    verilator_top->S_AXI_WVALID  = 1;
    eval();
    verilator_top->S_AXI_AWVALID = 0;
    verilator_top->S_AXI_WVALID  = 0;
    eval();
}

void v_send(int data[], int size){
    verilator_top->S_AXIS_TVALID = 1;
    for(int i=0; i<size; i++){
        verilator_top->S_AXIS_TDATA = data[i];
        eval();
    }
    verilator_top->S_AXIS_TVALID = 0;
    eval();
}

void v_receive(int data[], int size){
    while(verilator_top->M_AXIS_TVALID== 0)
        eval();
    for(int i=0; i<size; i+=1){
        data[i] = verilator_top->M_AXIS_TDATA;
        eval();
    }
    eval();
}

main(int argc, char **argv, char **env) {
    //////////////////// initialize mmap
    int fd = open("./tb.txt", O_RDWR, S_IRUSR | S_IWUSR);
    if(fd == -1){
        printf("file open error\n");
        exit(1);
    }
    struct stat st;
    if(fstat(fd, &st) < 0){
        exit(1);
    }
    volatile char *buf = (char *)mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    //////////////////// initialize verilator
    Verilated::commandArgs(argc,argv);
    Verilated::traceEverOn(true);
    main_time = 0;
    verilator_top = new Vtop;
    tfp = new VerilatedVcdC;
    verilator_top->trace(tfp, 99); // requires explicit max levels param
    tfp->open("tmp.vcd");
    main_time = 0;

    while(1){
        if(buf[0] != 0){
            if(buf[0] == 1){
                v_init();
            }
            else if(buf[0] == 2){
                buf[0] = 0;
                v_finish();
                break;
            }
            else if(buf[0] == 3){
                union int_char address, data;
                for(int i=0; i<4; i++){
                    address.c[i] = buf[i+4];
                    data.c[i] = buf[i+8];
                }
                v_write(address.i, data.i);
            }
            else if(buf[0] == 4){
                int array[64];
                union int_char data, size;
                for(int i=0; i<4; i++){
                    size.c[i] = buf[i+4];
                }
                for(int i=0; i<size.i; i++){
                    for(int j=0; j<4; j++){
                        data.c[j] = buf[i*4+j+8];
                    }
                    array[i] = data.i;
                }
                v_send(array, size.i);
            }                
            else if(buf[0] == 5){
                int array[64];
                union int_char data, size;
                for(int i=0; i<4; i++){
                    size.c[i] = buf[i+4];
                }
                v_receive(array, size.i);
                for(int i=0; i<size.i; i++){
                    data.i = array[i];
                    for(int j=0; j<4; j++){
                        buf[i*4+j+8] = data.c[j];
                    }
                }
            }
            buf[0] = 0;
        }
    }

    // post process /////////////////////////////
    eval();eval();eval();eval();eval();
    delete verilator_top;
    tfp->close();

    munmap((void*)buf, st.st_size);

    return 0;    
}