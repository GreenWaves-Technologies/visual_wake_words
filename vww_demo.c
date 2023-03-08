
/*
 * Copyright (C) 2017 GreenWaves Technologies
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license.  See the LICENSE file for details.
 *
 */


/* Autotiler includes. */
#include "vww.h"
#include "vwwKernels.h"
#include "gaplib/fs_switch.h"

#define H_INP 120
#define W_INP 160
#define CHANNELS 3

#define H_CAM 240
#define W_CAM 320
#define BYTES_CAM 2

AT_EMRAMFLASH_EXT_ADDR_TYPE vww_L3_Flash = 0;
short int Output_1[2];

pi_device_t Ram;
static struct pi_default_ram_conf ram_conf;
uint8_t ext_ram_buf;

static pi_task_t ctrl_tasks[2];
static pi_task_t ram_tasks[2];
static int remaining_size;
static int saved_size = 0;
static volatile int done = 0;
static int nb_transfers = 0;
static int count_transfers = 0;
static unsigned char current_buff = 0;
static int current_task = 0;
static int current_size[2];
static void handle_transfer_end(void *arg);
static void handle_ram_end(void *arg);
PI_L2 unsigned char *iter_buff[2];


// 2 Rows
#define ITER_SIZE (W_CAM*2*2)
#define RAW_SIZE (W_CAM*H_CAM*2) // For now only 10 bits config works

static pi_event_t proc_task;

static struct pi_device camera;
PI_L2 unsigned char *buff[2];

// This is called to enqueue new transfers
static void enqueue_transfer() {
    // We can enqueue new transfers if there are still a part of the image to
    // capture and less than 2 transfers are pending (the dma supports up to 2 transfers
    // at the same time)

    while (remaining_size > 0 && nb_transfers < 2) {
        int iter_size = (remaining_size < ITER_SIZE) ? remaining_size : ITER_SIZE;
        pi_task_t *task = &ctrl_tasks[current_task];

        // Enqueue a transfer. The callback will be called once the transfer is finished
        // so that  a new one is enqueued while another one is already running
        pi_camera_capture_async(&camera, iter_buff[current_task], iter_size, pi_evt_callback_no_irq_init(task, handle_transfer_end, (void *)current_task));

        current_size[current_task] = iter_size;
        remaining_size -= iter_size;
        nb_transfers++;
        current_task ^= 1;
    }

}

static void handle_transfer_end(void *arg) {
    nb_transfers--;
    unsigned char current_buff = (unsigned char) arg;

    enqueue_transfer();
    pi_task_t *task = &ram_tasks[current_buff];
    unsigned char * img = iter_buff[current_buff];
    int rgb_idx=0;
    for (int a = 0; a < W_CAM; a+=2) {
        // Shifts bits to delete the 2 LSB, on the 10 useful bits
        
        #if 0 //This is using grayscale
        uint16_t px = (img[a*2+1] << 6) | (img[a*2] >> 2);
        px += (img[a*2+1+2] << 6) | (img[a*2+2] >> 2);
        px += (img[a*2+1+(W_CAM*2)] << 6) | (img[a*2+(W_CAM*2)] >> 2);
        px += (img[a*2+1+(W_CAM*2)+2] << 6) | (img[a*2+(W_CAM*2)+2] >> 2);
        img[rgb_idx++] = px/4;
        img[rgb_idx++] = px/4;
        img[rgb_idx++] = px/4;
        #else
        uint16_t px = (img[a*2+1+(W_CAM*2)] << 6) | (img[a*2+(W_CAM*2)] >> 2);
        px += (img[a*2+1+2] << 6) | (img[a*2+2] >> 2);

        img[rgb_idx++] = (img[a*2+1] << 6) | (img[a*2] >> 2);
        img[rgb_idx++] = px/2;
        img[rgb_idx++] = (img[a*2+1+(W_CAM*2)+2] << 6) | (img[a*2+(W_CAM*2)+2] >> 2);
        #endif

    }
    pi_ram_write_async(&Ram, (ext_ram_buf + count_transfers*W_INP*3), img, (uint32_t) W_INP*3, pi_evt_callback_no_irq_init(&ram_tasks[current_buff], handle_ram_end, NULL));

    count_transfers++;
}

static void handle_ram_end(void *arg) {
    saved_size += W_INP*3;
    if (nb_transfers == 0 && saved_size == H_INP*W_INP*CHANNELS) {
        pi_evt_push(&proc_task);
    }
}

static int open_camera(struct pi_device *device)
{
    printf("Opening CSI2 camera\n");

    struct pi_ov5647_conf cam_conf;
    pi_ov5647_conf_init(&cam_conf);

    cam_conf.format=PI_CAMERA_QVGA;
    pi_open_from_conf(device, &cam_conf);
    if (pi_camera_open(device))
        return -1;

    return 0;
}

int last_seen = -10;
int counter = 0;
static void cluster()
{
    counter++;

    vwwCNN(Output_1);
    if (Output_1[0] < Output_1[1]) {
        last_seen = counter;
        printf("seen\n");
        pi_gpio_pin_write(PI_PAD_086, 1);
    }

    if (counter > (last_seen + 5)) {
        pi_gpio_pin_write(PI_PAD_086, 0);
    }

}

int test_vww(void)
{
    printf("Entering main controller\n");

    /* Configure And open cluster. */
    struct pi_device cluster_dev;
    struct pi_cluster_conf cl_conf;
    pi_cluster_conf_init(&cl_conf);
    cl_conf.cc_stack_size = STACK_SIZE;

    cl_conf.id = 0; /* Set cluster ID. */
                    // Enable the special icache for the master core
    cl_conf.icache_conf = PI_CLUSTER_MASTER_CORE_ICACHE_ENABLE |
                    // Enable the prefetch for all the cores, it's a 9bits mask (from bit 2 to bit 10), each bit correspond to 1 core
                    PI_CLUSTER_ICACHE_PREFETCH_ENABLE |
                    // Enable the icache for all the cores
                    PI_CLUSTER_ICACHE_ENABLE;

    pi_open_from_conf(&cluster_dev, (void *) &cl_conf);
    if (pi_cluster_open(&cluster_dev))
    {
        printf("Cluster open failed !\n");
        pmsis_exit(-4);
    }

    /* Frequency Settings: defined in the Makefile */
    int cur_fc_freq = pi_freq_set(PI_FREQ_DOMAIN_FC, FREQ_FC*1000*1000);
    int cur_cl_freq = pi_freq_set(PI_FREQ_DOMAIN_CL, FREQ_CL*1000*1000);
    int cur_pe_freq = pi_freq_set(PI_FREQ_DOMAIN_PERIPH, FREQ_PE*1000*1000);
    if (cur_fc_freq == -1 || cur_cl_freq == -1 || cur_pe_freq == -1)
    {
        printf("Error changing frequency !\nTest failed...\n");
        pmsis_exit(-4);
    }
    printf("FC Frequency = %d Hz CL Frequency = %d Hz PERIPH Frequency = %d Hz\n", 
            pi_freq_get(PI_FREQ_DOMAIN_FC), pi_freq_get(PI_FREQ_DOMAIN_CL), pi_freq_get(PI_FREQ_DOMAIN_PERIPH));
    

    // IMPORTANT - MUST BE CALLED AFTER THE CLUSTER IS SWITCHED ON!!!!
    printf("Constructor\n");
    int ConstructorErr = vwwCNN_Construct();
    if (ConstructorErr)
    {
        printf("Graph constructor exited with error: %d\n(check the generated file vwwKernels.c to see which memory have failed to be allocated)\n", ConstructorErr);
        pmsis_exit(-6);
    }

    struct pi_cluster_task task_ctor;
    pi_cluster_task(&task_ctor, (void (*)(void *)) vwwCNN_ConstructCluster, NULL);
    pi_cluster_send_task_to_cl(&cluster_dev, &task_ctor);

    printf("Call cluster\n");
    struct pi_cluster_task task;
    pi_cluster_task(&task, (void (*)(void *))cluster, NULL);
    pi_cluster_task_stacks(&task, NULL, SLAVE_STACK_SIZE);


    //Open camera
    if (open_camera(&camera))
    {
        printf("Failed to open camera\n");
        return -1;
    }
    printf("Turning camera on...\n");
    //turn on camera
    pi_camera_control(&camera, PI_CAMERA_CMD_ON, 0);
    // Allocate ping pong buffers for Camera read
    iter_buff[0] = pi_l2_malloc(ITER_SIZE);
    if (iter_buff[0] == NULL) return -1;
    iter_buff[1] = pi_l2_malloc(ITER_SIZE);
    if (iter_buff[1] == NULL) return -1;

    /* Init & open ram. */
    pi_default_ram_conf_init(&ram_conf);
    pi_open_from_conf(&Ram, &ram_conf);

    pi_evt_sig_init(&proc_task);
    printf("open ram\n");
    if (pi_ram_open(&Ram)) {
        printf("Error ram open !\n");
        pmsis_exit(-5);
    }
    printf("ram opened\n");
    if (pi_ram_alloc(&Ram, (uint32_t *) ext_ram_buf, H_INP * W_INP * CHANNELS) != 0) {
        printf("Failed to allocate memory in external ram (%ld bytes)\n", H_INP * W_INP * CHANNELS);
        pmsis_exit(-1);
    }

    pi_gpio_flags_e flags = PI_GPIO_OUTPUT;
    pi_gpio_pin_configure(PI_PAD_086, flags);
    pi_gpio_pin_write(PI_PAD_086, 0);

    while(1){
        remaining_size = RAW_SIZE;
        saved_size=0;
        nb_transfers=0;
        count_transfers=0;
        current_buff=0;
        done=0;
        current_task = 0;

        enqueue_transfer();
        pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
        pi_evt_wait(&proc_task);
        pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
        
        //Copy from ram to: main_L2_Memory_Dyn + (H_INP * W_INP * CHANNELS)
        //The image is saved onto External
        pi_ram_read(&Ram, ext_ram_buf, Input_1, (uint32_t)(H_INP * W_INP * CHANNELS));

        pi_cluster_send_task_to_cl(&cluster_dev, &task);

        pi_evt_sig_init(&proc_task);

    }

    vwwCNN_Destruct();


    printf("Ended\n");
    pmsis_exit(0);
    return 0;
}

int main(int argc, char *argv[])
{
    printf("\n\n\t *** NNTOOL vww Example ***\n\n");
    test_vww();
    return 0;
}
