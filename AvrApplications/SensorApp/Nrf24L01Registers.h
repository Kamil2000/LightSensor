#ifndef NRF24L01REGISTERS_H_
#define NRF24L01REGISTERS_H_

#define NRF_CONFIG_REG             0x00
#define NRF_CONFIG_BIT_MASK_RX_DR  6
#define NRF_CONFIG_BIT_MASK_TX_DS  5
#define NRF_CONFIG_BIT_MASK_MAX_RT 4
#define NRF_CONFIG_BIT_EN_CRC      3
#define NRF_CONFIG_BIT_CRCO        2
#define NRF_CONFIG_BIT_PWR_UP      1
#define NRF_CONFIG_BIT_PRIM_RX     0

#define NRF_EN_AA_REG             0x01
#define NRF_EN_AA_BIT_ENAA_P5     5
#define NRF_EN_AA_BIT_ENAA_P4     4
#define NRF_EN_AA_BIT_ENAA_P3     3
#define NRF_EN_AA_BIT_ENAA_P2     2
#define NRF_EN_AA_BIT_ENAA_P1     1
#define NRF_EN_AA_BIT_ENAA_P0     0

#define NRF_EN_RXADDR_REG             0x02
#define NRF_EN_RXADDR_BIT_ERX_P5      5
#define NRF_EN_RXADDR_BIT_ERX_P4      4
#define NRF_EN_RXADDR_BIT_ERX_P3      3
#define NRF_EN_RXADDR_BIT_ERX_P2      2
#define NRF_EN_RXADDR_BIT_ERX_P1      1
#define NRF_EN_RXADDR_BIT_ERX_P0      0

#define NRF_SETUP_AW_REG    0x03
#define NRF_SETUP_AW_BIT_AW 0 /*0:1*/

#define NRF_SETUP_RETR_REG     0x04
#define NRF_SETUP_RETR_BIT_ARD 4 /*7:4*/
#define NRF_SETUP_RETR_BIT_ARC 0 /*3:0*/

#define NRF_RF_CH_REG       0x05 /*6:0*/
#define NRF_RF_CH_REG_MAX   127

#define NRF_RF_SETUP_REG    0x06
#define NRF_RF_SETUP_BIT_PLL_LOCK    4
#define NRF_RF_SETUP_BIT_RF_DR       3
#define NRF_RF_SETUP_BIT_RF_PWR      1 /*2:1*/
#define NRF_RF_SETUP_BIT_LNA_HCURR   0

#define NRF_STATUS_REG      0x07
#define NRF_STATUS_BIT_RX_DR       6
#define NRF_STATUS_BIT_TX_DS       5
#define NRF_STATUS_BIT_MAX_RT      4
#define NRF_STATUS_BIT_RX_P_NO     1
#define NRF_STATUS_BIT_TX_FULL     0

#define NRF_OBSERVE_TX_REG  0x08
#define NRF_OBSERVE_TX_BIT_PLOS_CNT    4 /*7:4*/
#define NRF_OBSERVE_TX_BIT_ARC_CNT     0 /*3:0*/

#define NRF_CD_REG          0x09
#define NRF_CD_BIT_CD       0

#define NRF_RX_ADDR_P0_REG  0x0A /*39-0*/
#define NRF_RX_ADDR_P1_REG  0x0B /*39-0*/
#define NRF_RX_ADDR_P2_REG  0x0C /*7-0 for MSB address value matches 39:8 from P1*/
#define NRF_RX_ADDR_P3_REG  0x0D /*7-0 for MSB address value matches 39:8 from P1*/
#define NRF_RX_ADDR_P4_REG  0x0E /*7-0 for MSB address value matches 39:8 from P1*/
#define NRF_RX_ADDR_P5_REG  0x0F /*7-0 for MSB address value matches 39:8 from P1*/

#define NRF_TX_ADDR_REG     0x10 /*39:0*/

#define NRF_RX_PW_P0_REG    0x11 /*5:0*/ 

#define NRF_RX_PW_P1_REG    0x12 /*5:0*/
 

#define NRF_RX_PW_P2_REG    0x13 /*5:0*/ 

#define NRF_RX_PW_P3_REG    0x14 /*5:0*/ 

#define NRF_RX_PW_P4_REG    0x15 /*5:0*/

#define NRF_RX_PW_P5_REG    0x16 /*5:0*/

#define NRF_FIFO_STATUS_REG 0x17
#define NRF_FIFO_STATUS_BIT_TX_REUSE    6
#define NRF_FIFO_STATUS_BIT_FIFO_FULL   5
#define NRF_FIFO_STATUS_BIT_TX_EMPTY    4
#define NRF_FIFO_STATUS_BIT_RX_FULL     1
#define NRF_FIFO_STATUS_BIT_RX_EMPTY    0

#define NRF_DYNPD_REG	    0x1C
#define NRF_DYNPD_BIT_DPL_P5	    5
#define NRF_DYNPD_BIT_DPL_P4	    4
#define NRF_DYNPD_BIT_DPL_P3	    3
#define NRF_DYNPD_BIT_DPL_P2	    2
#define NRF_DYNPD_BIT_DPL_P1	    1
#define NRF_DYNPD_BIT_DPL_P0	    0

#define NRF_FEATURE_REG	    0x1D
#define NRF_FEATURE_BIT_EN_DPL	    2
#define NRF_FEATURE_BIT_EN_ACK_PAY  1
#define NRF_FEATURE_BIT_EN_DYN_ACK  0

#define NRF_R_REGISTER    0x00
#define NRF_W_REGISTER    0x20

#define NRF_ACTIVATE_INST      0x50
#define NRF_R_RX_PL_WID_INST   0x60
#define NRF_R_RX_PAYLOAD_INST  0x61
#define NRF_W_TX_PAYLOAD_INST  0xA0
#define NRF_W_ACK_PAYLOAD_INST 0xA8
#define NRF_FLUSH_TX_INST      0xE1
#define NRF_FLUSH_RX_INST      0xE2
#define NRF_REUSE_TX_PL_INST   0xE3
#define NRF_NOP_INST           0xFF
#define NRF_GET_STATUS_INST  NRF_NOP_INST

#define NRF_REGISTER_MASK      0x1F


#endif /* NRF24L01REGISTERS_H_ */