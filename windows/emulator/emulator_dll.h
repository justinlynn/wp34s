/*
 *  emulator_dll.c
 *
 *  This is the include file for using HP's HP20b emulator kernel as a DLL
 *  
 *  In WinMain, call
 *  init_emulator( name, init, KeyPress, updatescreen, ScrollTopLine, 
 *                 GetFlag, SetFlag, ClearFlag, GetOffset, GetBottomLine,
 *                 CheckCommmunication );
 *  NULLs are valid pointers and are replaced by default behaviour.
 *
 *  Written by Marcus von Cube
 */
#ifdef __cplusplus
extern "C" {
#else
#define bool int
#define true 1
#define false 0
#endif

#include <windows.h>

#ifdef _WINDLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __declspec(dllimport)
#endif

int EXPORT start_emulator( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow,
			   char *name,
			   long long builddate,
			   unsigned int *p_LcdData,
			   void *p_Init,
			   void *p_Reset,
			   void *p_Shutdown,
			   void *p_KeyPress,
			   void *p_UpdateScreen,
			   void *p_ScrollTopLine,
			   void *p_GetFlag,
			   void *p_SetFlag,
			   void *p_ClearFlag,
			   void *p_GetOffset,
			   void *p_GetTopLine,
			   void *p_GetBottomLine,
			   void *p_CheckCommunication );

void EXPORT UpdateDlgScreen(int force);

void EXPORT AddKey(int k,bool ifnotfull);

int  EXPORT KeyBuffEmpty(void);

#ifdef __cplusplus
}
#endif
