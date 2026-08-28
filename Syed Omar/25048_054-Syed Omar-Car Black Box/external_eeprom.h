#ifndef XC_HEADER_TEMPLATE_H
#define	XC_HEADER_TEMPLATE_H

#include <xc.h> 
#ifdef	__cplusplus
extern "C" {
#endif 
#ifdef	__cplusplus
}
#endif 
#endif	
#ifndef external_eeprom
#define external_eeprom
#define SLAVE_READ_E		0xA1
#define SLAVE_WRITE_E		0xA0
void write_external_eeprom(unsigned char address1,  unsigned char data);
unsigned char read_external_eeprom(unsigned char address1);
#endif