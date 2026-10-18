#pragma once
/*
 * Board constants for the shim. Build with BOARD=grus (shim/build.sh) for the
 * Xiaomi Mi 9 SE (grus, SDM712); the default is the Redmi Note 10 Pro (sweet).
 *
 * grus values come from Xiaomi's grus-p-oss kernel tree:
 *   sdm670.dtsi       cont_splash_region@9c000000 (0x2300000), msm-imem@146bf000
 *                     restart_reason@65c, pshold 0xc264000, GIC 0x17a00000 /
 *                     0x17a60000, ufshc@1d84000
 *   sdm670-sde.dtsi   mdss_mdp@ae00000: CTL 0x2000 + 0x200 n, PP 0x71000 + 0x800 n,
 *                     SSPP 0x5000 0x7000 0x25000 0x27000 0x29000
 *   dsi-panel-samsung-fhd-ea8076-cmd.dtsi
 *                     1080x2340, DSI command mode (ea8074 variant: same)
 */

#if defined(BOARD_GRUS)
#define BOARD_NAME		"grus"
#define IMEM_RESTART_REASON	0x146bf65cUL	/* sdm670 msm-imem@146bf000 + 0x65c */
#define FB_W			1080
#define FB_H			2340
#define FB_SIZE			0x2300000UL
#define BOARD_CMD_PANEL		1
#define MDP_BASE		0x0ae00000UL
#else
#define BOARD_NAME		"sweet"
#define IMEM_RESTART_REASON	0x146aa65cUL	/* qcom,msm-imem restart_reason@65c */
#define FB_W			1080
#define FB_H			2400
#define FB_SIZE			0x1700000UL
#define BOARD_CMD_PANEL		0
#endif

#define FB_PHYS			0x9c000000UL	/* reserved-memory cont_splash_region */
#define FB_STRIDE		(FB_W * 4)	/* a8r8g8b8, matches mainline simplefb */
#define PSHOLD			0x0c264000UL
#define GICD_BASE		0x17a00000UL
#define GICR_BASE		0x17a60000UL
#define UFS_BASE		0x01d84000UL	/* ufshc@1d84000 on both */
