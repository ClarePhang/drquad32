/** Debounce filters. See TC1767 Users' manual */


/**
 * Signal muss f�r eine gewisse Zeit stabil sein,
 * bevor der Pegel �bernommen wird.
 *
 *
 *
 * Verz�gert das Signal um die Filterzeit.
 */
bool debounce_delayed(bool signal)
{
}


/**
 * Neuer Pegel wird sofort �bernommen.
 * Folgende Flanken werden eine gewisse Zeit ignoriert.
 *
 * Keine verz�gerung. Kurze glitches werden jedoch auf
 * die Filterzeit verl�ngert und nicht rausgefiltert.
 *
 * Filterzeit wird nicht retriggert!
 * 
 * - Edge einstellbar (rising, falling, both)
 *   (Bei rising f�hren nur steigende flnaken zu einer inhibition time)
 *
 */
bool debounce_immediate(bool signal)
{
}


bool debounce_mixed(bool signal)
{

}