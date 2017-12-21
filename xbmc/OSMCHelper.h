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
// OSMCHelper.h: routines to improve behaviour of Kodi on OSMC                             
//                                                                                         
//////////////////////////////////////////////////////////////////////                     
                                                                                           
#pragma once                                                                               
                                                                                           
#include <sys/syscall.h>                                                                   
#include <sys/types.h>                                                                     
#include <sys/utsname.h>                                                                   
#include <unistd.h>                                                                        
#include <stdio.h>                                                                         
#include <string.h>                                                                        
                                                                                           
extern "C" {                                                                               
    #if defined(__arm__)                                                                   
      /* Fix up uname for 64-bit kernels with 32-bit userland */                           
      int uname(struct utsname *buf);                                                      
    #endif // __arm__                                                                      
}                                                                                          
