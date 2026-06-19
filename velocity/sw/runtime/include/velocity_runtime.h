#ifndef _FLEX_RUNTIME_H_
#define _FLEX_RUNTIME_H_
#include <stdint.h>
#include "velocity_arch.h"
#include "velocity_dma.h"

#define local(offset)               (ARCH_CLUSTER_TCDM_BASE+offset)
#define zomem(offset)               (ARCH_CLUSTER_ZOMEM_BASE+offset)

#define VELOCITY_CTRL_REG_EOC             0x00
#define VELOCITY_CTRL_REG_EOC_ALL         0x04
#define VELOCITY_CTRL_REG_TIMER_START     0x08
#define VELOCITY_CTRL_REG_TIMER_END_PRINT 0x0c
#define VELOCITY_CTRL_REG_LOG_CHAR        0x10
#define VELOCITY_CTRL_REG_LOG_INT         0x14
#define VELOCITY_CTRL_REG_TIME_LO         0x18
#define VELOCITY_CTRL_REG_TIME_HI         0x1c
#define VELOCITY_CTRL_REG_TIMER_LO        0x20
#define VELOCITY_CTRL_REG_TIMER_HI        0x24
#define VELOCITY_CTRL_REG_BARRIER_ARRIVE  0x28
#define VELOCITY_CTRL_REG_BARRIER_PHASE   0x2c
#define VELOCITY_CTRL_REG_BARRIER_COUNT   0x30
#define VELOCITY_CTRL_REG_CHIP_ID         0x34
#define VELOCITY_CTRL_REG_NUM_CHIP        0x38

static inline volatile uint32_t *velocity_ctrl_reg(uint32_t offset)
{
    return (volatile uint32_t *)(uintptr_t)(ARCH_SOC_REGISTER_EOC + offset);
}

static inline uint64_t velocity_ctrl_read64(uint32_t lo_offset, uint32_t hi_offset)
{
    uint32_t hi;
    uint32_t lo;
    uint32_t hi_check;

    do
    {
        hi = *velocity_ctrl_reg(hi_offset);
        lo = *velocity_ctrl_reg(lo_offset);
        hi_check = *velocity_ctrl_reg(hi_offset);
    } while (hi != hi_check);

    return ((uint64_t)hi << 32) | lo;
}

/*******************
*  Core Position   *
*******************/

uint32_t flex_get_core_id(){
    uint32_t hartid;
    asm volatile("csrr %0, mhartid" : "=r"(hartid));
    return hartid;
}

uint32_t flex_chip_id(){
    return *velocity_ctrl_reg(VELOCITY_CTRL_REG_CHIP_ID);
}

uint32_t flex_num_chip(){
    return *velocity_ctrl_reg(VELOCITY_CTRL_REG_NUM_CHIP);
}

static inline velocity_dma_port_t flex_dma_port(void)
{
    return velocity_dma_make_port(
        ARCH_DMA_REG_OFFSET,
        ARCH_NUM_CLUSTER,
        flex_get_core_id(),
        ARCH_DMA_CLUSTER_STRIDE);
}

static inline velocity_dma_port_t flex_rdma_port(void)
{
    return velocity_dma_make_port(
        ARCH_RDMA_REG_OFFSET,
        ARCH_NUM_CHIP,
        flex_chip_id(),
        ARCH_RDMA_CHIP_STRIDE);
}

/*******************
*        EoC       *
*******************/

void flex_eoc(uint32_t val){
    *velocity_ctrl_reg(VELOCITY_CTRL_REG_EOC) = val;
}

void flex_eoc_all(uint32_t val){
    *velocity_ctrl_reg(VELOCITY_CTRL_REG_EOC_ALL) = val;
}

/*******************
*   Perf Counter   *
*******************/

void flex_timer_start(){
    *velocity_ctrl_reg(VELOCITY_CTRL_REG_TIMER_START) = 1;
}

void flex_timer_end(){
    *velocity_ctrl_reg(VELOCITY_CTRL_REG_TIMER_END_PRINT) = 1;
}

uint64_t flex_time_ps(){
    return velocity_ctrl_read64(VELOCITY_CTRL_REG_TIME_LO, VELOCITY_CTRL_REG_TIME_HI);
}

uint64_t flex_timer_elapsed_ps(){
    return velocity_ctrl_read64(VELOCITY_CTRL_REG_TIMER_LO, VELOCITY_CTRL_REG_TIMER_HI);
}

uint64_t flex_timer_elapsed_ns(){
    return flex_timer_elapsed_ps() / 1000;
}

/*******************
* Virtual Barrier  *
*******************/

uint32_t flex_barrier_phase(){
    return *velocity_ctrl_reg(VELOCITY_CTRL_REG_BARRIER_PHASE);
}

uint32_t flex_barrier_count(){
    return *velocity_ctrl_reg(VELOCITY_CTRL_REG_BARRIER_COUNT);
}

void flex_barrier_all(){
    *velocity_ctrl_reg(VELOCITY_CTRL_REG_BARRIER_ARRIVE) = 1;
}

/*******************
*      Logging     *
*******************/

void flex_log_char(char c){
    uint32_t data = (uint32_t) c;
    *velocity_ctrl_reg(VELOCITY_CTRL_REG_LOG_CHAR) = data;
}

void flex_print(char * str){
    for (int i = 0; str[i] != '\0'; i++) {
        flex_log_char(str[i]);
    }
}

void flex_print_int(uint32_t data){
    *velocity_ctrl_reg(VELOCITY_CTRL_REG_LOG_INT) = data;
}

#endif
