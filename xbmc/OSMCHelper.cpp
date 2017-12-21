/*                                                   
 *      Copyright (C) 2005-2013 Team XBMC            
 *      http://xbmc.org                              
 *                                                   
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option) 
 *  any later version.                                                  
 *                                                                      
 *  This Program is distributed in the hope that it will be useful,     
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of      
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the        
 *  GNU General Public License for more details.                        
 *                                                                      
 *  You should have received a copy of the GNU General Public License   
 *  along with XBMC; see the file COPYING.  If not, see                 
 *  <http://www.gnu.org/licenses/>.                                     
 *                                                                      
 */                                                                     
// OSMCHelper.cpp: implementation of OSMC helper routines
//                                                                       
//////////////////////////////////////////////////////////////////////   
                                                                         
extern "C" {                                                            
#include "OSMCHelper.h"                                                 
   #if defined(__arm__)                                                 
     /* Ensure that uname returns arm, or machine model will reflect kernel bitness only */
     int uname(struct utsname *buf)                                                        
     {                                                                                     
       int r;                                                                              
       r = syscall(SYS_uname, buf);                                                        
       strcpy(buf->machine, "armv7");                                                        
       return r;                                                                           
     }                                                                                     
   #endif // __arm__                                                                       
}                                                                                          
