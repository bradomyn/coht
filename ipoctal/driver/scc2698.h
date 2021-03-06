/*
 * scc2698.h 
 *
 * driver for the IPOCTAL boards
 * Copyright (c) 2009 Nicolas Serafini, EIC2 SA
 * Copyright (c) 2010,2011 Samuel Iglesias Gonsalvez <siglesia@cern.ch>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef SCC2698_H_
#define SCC2698_H_

/*
 * struct scc2698_channel - Channel access to scc2698 IO 
 *
 * dn value are only spacer.
 * 
 */
struct scc2698_channel{
    union {
        struct {
            unsigned char d0, mr;  /* Mode register 1/2*/
            unsigned char d1, sr;  /* Status register */
            unsigned char d2, r1;  /* reserved */
            unsigned char d3, rhr; /* Receive holding register (R) */
            unsigned char junk[8]; /* other crap for block control */
        } r; /* Read access */
        struct {
            unsigned char d0, mr;  /* Mode register 1/2 */
            unsigned char d1, csr; /* Clock select register */
            unsigned char d2, cr;  /* Command register */
            unsigned char d3, thr; /* Transmit holding register */
            unsigned char junk[8]; /* other crap for block control */
        } w; /* Write access */
    } u;
};

/*
 * struct scc2698_block - Block access to scc2698 IO
 *
 * The scc2698 contain 4 block.
 * Each block containt two channel a and b.
 * dn value are only spacer.
 *
 */
struct scc2698_block{
    union {
        struct {
            unsigned char d0, mra;  /* Mode register 1/2 (a) */
            unsigned char d1, sra;  /* Status register (a) */
            unsigned char d2, r1;   /* reserved */
            unsigned char d3, rhra; /* Receive holding register (a) */
            unsigned char d4, ipcr; /* Input port change register of block */
            unsigned char d5, isr;  /* Interrupt status register of block */
            unsigned char d6, ctur; /* Counter timer upper register of block */
            unsigned char d7, ctlr; /* Counter timer lower register of block */
            unsigned char d8, mrb;  /* Mode register 1/2 (b) */
            unsigned char d9, srb;  /* Status register (b) */
            unsigned char da, r2;   /* reserved */
            unsigned char db, rhrb; /* Receive holding register (b) */
            unsigned char dc, r3;   /* reserved */
            unsigned char dd, ip;   /* Input port register of block */
            unsigned char de, ctg;  /* Start counter timer of block */
            unsigned char df, cts;  /* Stop counter timer of block */
        } r; /* Read access */
        struct {
            unsigned char d0, mra;  /* Mode register 1/2 (a) */
            unsigned char d1, csra; /* Clock select register (a) */
            unsigned char d2, cra;  /* Command register (a) */
            unsigned char d3, thra; /* Transmit holding register (a) */
            unsigned char d4, acr;  /* Auxiliary control register of block */
            unsigned char d5, imr;  /* Interrupt mask register of block  */
            unsigned char d6, ctu;  /* Counter timer upper register of block */
            unsigned char d7, ctl;  /* Counter timer lower register of block */
            unsigned char d8, mrb;  /* Mode register 1/2 (b) */
            unsigned char d9, csrb; /* Clock select register (a) */
            unsigned char da, crb;  /* Command register (b) */
            unsigned char db, thrb; /* Transmit holding register (b) */
            unsigned char dc, r1;   /* reserved */
            unsigned char dd, opcr; /* Output port configuration register of block */
            unsigned char de, r2;   /* reserved */
            unsigned char df, r3;   /* reserved */
        } w; /* Write access */
    } u;
} ;

#define MR1_CHRL_5_BITS             (0x0 << 0x0)
#define MR1_CHRL_6_BITS             (0x1 << 0x0)
#define MR1_CHRL_7_BITS             (0x2 << 0x0)
#define MR1_CHRL_8_BITS             (0x3 << 0x0)
#define MR1_PARITY_EVEN             (0x1 << 0x2)
#define MR1_PARITY_ODD              (0x0 << 0x2)
#define MR1_PARITY_ON               (0x0 << 0x3)
#define MR1_PARITY_FORCE            (0x1 << 0x3)
#define MR1_PARITY_OFF              (0x2 << 0x3)
#define MR1_PARITY_SPECIAL          (0x3 << 0x3)
#define MR1_ERROR_CHAR              (0x0 << 0x5)
#define MR1_ERROR_BLOCK             (0x1 << 0x5)
#define MR1_RxINT_RxRDY             (0x0 << 0x6)
#define MR1_RxINT_FFULL             (0x1 << 0x6)
#define MR1_RxRTS_CONTROL_ON        (0x1 << 0x7)
#define MR1_RxRTS_CONTROL_OFF       (0x0 << 0x7)

#define MR2_STOP_BITS_LENGTH_1      (0x7 << 0x0)
#define MR2_STOP_BITS_LENGTH_2      (0xF << 0x0)
#define MR2_CTS_ENABLE_TX_ON        (0x1 << 0x4)
#define MR2_CTS_ENABLE_TX_OFF       (0x0 << 0x4)
#define MR2_TxRTS_CONTROL_ON        (0x1 << 0x5)
#define MR2_TxRTS_CONTROL_OFF       (0x0 << 0x5)
#define MR2_CH_MODE_NORMAL          (0x0 << 0x6)
#define MR2_CH_MODE_ECHO            (0x1 << 0x6)
#define MR2_CH_MODE_LOCAL           (0x2 << 0x6)
#define MR2_CH_MODE_REMOTE          (0x3 << 0x6)

#define CR_ENABLE_RX                (0x1 << 0x0)
#define CR_DISABLE_RX               (0x1 << 0x1)
#define CR_ENABLE_TX                (0x1 << 0x2)
#define CR_DISABLE_TX               (0x1 << 0x3)
#define CR_CMD_RESET_MR             (0x1 << 0x4)
#define CR_CMD_RESET_RX             (0x2 << 0x4)
#define CR_CMD_RESET_TX             (0x3 << 0x4)
#define CR_CMD_RESET_ERR_STATUS     (0x4 << 0x4)
#define CR_CMD_RESET_BREAK_CHANGE   (0x5 << 0x4)
#define CR_CMD_START_BREAK          (0x6 << 0x4)
#define CR_CMD_STOP_BREAK           (0x7 << 0x4)
#define CR_CMD_ASSERT_RTSN          (0x8 << 0x4)
#define CR_CMD_NEGATE_RTSN          (0x9 << 0x4)
#define CR_CMD_SET_TIMEOUT_MODE     (0xA << 0x4)
#define CR_CMD_DISABLE_TIMEOUT_MODE (0xC << 0x4)

#define SR_RX_READY                 (0x1 << 0x0)
#define SR_FIFO_FULL                (0x1 << 0x1)
#define SR_TX_READY                 (0x1 << 0x2)
#define SR_TX_EMPTY                 (0x1 << 0x3)
#define SR_OVERRUN_ERROR            (0x1 << 0x4)
#define SR_PARITY_ERROR             (0x1 << 0x5)
#define SR_FRAMING_ERROR            (0x1 << 0x6)
#define SR_RECEIVED_BREAK           (0x1 << 0x7)

#define SR_ERROR                    (0xF0)

#define ACR_DELTA_IP0_IRQ_EN        (0x1 << 0x0)
#define ACR_DELTA_IP1_IRQ_EN        (0x1 << 0x1)
#define ACR_DELTA_IP2_IRQ_EN        (0x1 << 0x2)
#define ACR_DELTA_IP3_IRQ_EN        (0x1 << 0x3)
#define ACR_CT_Mask                 (0x7 << 0x4)
#define ACR_CExt                    (0x0 << 0x4)
#define ACR_CTxCA                   (0x1 << 0x4)
#define ACR_CTxCB                   (0x2 << 0x4)
#define ACR_CClk16                  (0x3 << 0x4)
#define ACR_TExt                    (0x4 << 0x4)
#define ACR_TExt16                  (0x5 << 0x4)
#define ACR_TClk                    (0x6 << 0x4)
#define ACR_TClk16                  (0x7 << 0x4)
#define ACR_BRG_SET1                (0x0 << 0x7)
#define ACR_BRG_SET2                (0x1 << 0x7)

#define TX_CLK_75                   (0x0 << 0x0)
#define TX_CLK_110                  (0x1 << 0x0)
#define TX_CLK_38400                (0x2 << 0x0)
#define TX_CLK_150                  (0x3 << 0x0)
#define TX_CLK_300                  (0x4 << 0x0)
#define TX_CLK_600                  (0x5 << 0x0)
#define TX_CLK_1200                 (0x6 << 0x0)
#define TX_CLK_2000                 (0x7 << 0x0)
#define TX_CLK_2400                 (0x8 << 0x0)
#define TX_CLK_4800                 (0x9 << 0x0)
#define TX_CLK_1800                 (0xA << 0x0)
#define TX_CLK_9600                 (0xB << 0x0)
#define TX_CLK_19200                (0xC << 0x0)
#define RX_CLK_75                   (0x0 << 0x4)
#define RX_CLK_110                  (0x1 << 0x4)
#define RX_CLK_38400                (0x2 << 0x4)
#define RX_CLK_150                  (0x3 << 0x4)
#define RX_CLK_300                  (0x4 << 0x4)
#define RX_CLK_600                  (0x5 << 0x4)
#define RX_CLK_1200                 (0x6 << 0x4)
#define RX_CLK_2000                 (0x7 << 0x4)
#define RX_CLK_2400                 (0x8 << 0x4)
#define RX_CLK_4800                 (0x9 << 0x4)
#define RX_CLK_1800                 (0xA << 0x4)
#define RX_CLK_9600                 (0xB << 0x4)
#define RX_CLK_19200                (0xC << 0x4)

#define OPCR_MPOa_RTSN              (0x0 << 0x0)
#define OPCR_MPOa_C_TO              (0x1 << 0x0)
#define OPCR_MPOa_TxC1X             (0x2 << 0x0)
#define OPCR_MPOa_TxC16X            (0x3 << 0x0)
#define OPCR_MPOa_RxC1X             (0x4 << 0x0)
#define OPCR_MPOa_RxC16X            (0x5 << 0x0)
#define OPCR_MPOa_TxRDY             (0x6 << 0x0)
#define OPCR_MPOa_RxRDY_FF          (0x7 << 0x0)

#define OPCR_MPOb_RTSN              (0x0 << 0x4)
#define OPCR_MPOb_C_TO              (0x1 << 0x4)
#define OPCR_MPOb_TxC1X             (0x2 << 0x4)
#define OPCR_MPOb_TxC16X            (0x3 << 0x4)
#define OPCR_MPOb_RxC1X             (0x4 << 0x4)
#define OPCR_MPOb_RxC16X            (0x5 << 0x4)
#define OPCR_MPOb_TxRDY             (0x6 << 0x4)
#define OPCR_MPOb_RxRDY_FF          (0x7 << 0x4)

#define OPCR_MPP_INPUT              (0x0 << 0x7)
#define OPCR_MPP_OUTPUT             (0x1 << 0x7)

#define IMR_TxRDY_A                 (0x1 << 0x0)
#define IMR_RxRDY_FFULL_A           (0x1 << 0x1)
#define IMR_DELTA_BREAK_A           (0x1 << 0x2)
#define IMR_COUNTER_READY           (0x1 << 0x3)
#define IMR_TxRDY_B                 (0x1 << 0x4)
#define IMR_RxRDY_FFULL_B           (0x1 << 0x5)
#define IMR_DELTA_BREAK_B           (0x1 << 0x6)
#define IMR_INPUT_PORT_CHANGE       (0x1 << 0x7)

#define ISR_TxRDY_A                 (0x1 << 0x0)
#define ISR_RxRDY_FFULL_A           (0x1 << 0x1)
#define ISR_DELTA_BREAK_A           (0x1 << 0x2)
#define ISR_COUNTER_READY           (0x1 << 0x3)
#define ISR_TxRDY_B                 (0x1 << 0x4)
#define ISR_RxRDY_FFULL_B           (0x1 << 0x5)
#define ISR_DELTA_BREAK_B           (0x1 << 0x6)
#define ISR_INPUT_PORT_CHANGE       (0x1 << 0x7)

#endif /* SCC2698_H_ */
