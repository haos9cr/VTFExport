// resource.h - control IDs for the VTF options dialog.
#pragma once

#define IDD_VTF_OPTIONS        101

#define IDC_FORMAT             1001
#define IDC_VERSION            1002
#define IDC_MIPMAPS            1003
#define IDC_GAMMA              1004
#define IDC_THUMB              1005
#define IDC_RESET              1006
#define IDC_INFO               1007
#define IDC_LANG               1008

#define IDC_RESIZE             1020
#define IDC_METHOD             1021
#define IDC_RFILTER            1022
#define IDC_MAXW               1023
#define IDC_MAXH               1024
#define IDC_MIPFILTER          1025
#define IDC_ANIMATE            1026
#define IDC_ORDER              1027
#define IDC_ANIM_HINT          1028

// Static texts / group boxes whose captions are localised at runtime.
#define IDC_LBL_LANG           1040
#define IDC_LBL_FORMAT         1041
#define IDC_LBL_VERSION        1042
#define IDC_GRP_FORMAT         1043
#define IDC_GRP_MIPS           1044
#define IDC_GRP_FLAGS          1045
#define IDC_GRP_RESIZE         1047
#define IDC_LBL_METHOD         1048
#define IDC_LBL_RFILTER        1049
#define IDC_LBL_MAXW           1050
#define IDC_LBL_MAXH           1051
#define IDC_LBL_MIPFILTER      1052
#define IDC_GRP_ANIM           1053
#define IDC_LBL_ORDER          1054

// Flag checkboxes: IDC_FLAG_FIRST + index into kFlags (VTFExportUI.cpp).
#define IDC_FLAG_FIRST         1100
#define IDC_FLAG_LAST          1199
