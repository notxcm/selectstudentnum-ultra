#ifndef RESOURCE_H
#define RESOURCE_H

/* 图标 */
#define IDI_ICON        101
/* 模糊背景图（嵌入资源，实现单文件） */
#define IDR_BG          300
/* 作者头像（状态栏署名用，嵌入资源） */
#define IDR_AVATAR      301

/* 设置对话框（已平铺到主窗口，控件 ID 直接用于主窗口设置面板） */
#define IDC_LBL_RANGES  210
#define IDC_ED_RANGES   211
#define IDC_LBL_MANUAL  212
#define IDC_ED_MANUAL   213
#define IDC_LBL_EXCLUDE 214
#define IDC_ED_EXCLUDE  215
#define IDC_LBL_ANIM    216
#define IDC_ED_ANIM     217
#define IDC_LBL_SCALE   218
#define IDC_ED_SCALE    219
#define IDC_CHK_SOUND   222
#define IDC_CHK_UNIQUE2 223
#define IDC_BTN_APPLY   224

/* Surprise 惊喜窗口 */
#define IDD_SURPRISE    201
#define IDC_ED_SUP      226
#define IDC_BTN_SUPCLEAR 227
#define IDC_CHK_SUP     228

#ifndef IDOK
#define IDOK 1
#endif
#ifndef IDCANCEL
#define IDCANCEL 2
#endif

/* 主窗口控件 */
#define IDC_DISPLAY     1
#define IDC_STATS       2
#define IDC_LBLHIST     3
#define IDC_HISTORY     4
#define IDC_BTNPICK     5
#define IDC_BTNRESET    6
#define IDC_BTNUNDO     7
#define IDC_BTNSETTINGS 8
#define IDC_BTNEXPORT   9
#define IDC_BTNCLEAR    10
#define IDC_BTNFULL     11
#define IDC_LBLN        12
#define IDC_EDN         13
#define IDC_CHKUNIQUE   14
#define IDC_LBLRECENT   15
#define IDC_EDRECENT    16
#define IDC_STATUS      17
#define IDC_SIG         18

/* 自定义消息 */
#define UM_PICK         (WM_USER + 1)

#endif
