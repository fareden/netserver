/*
 * Operación básica de cadenas, etc...
 */
#include <iostream>
#include <vector>
#include <list>
#include <iterator>
#include <string.h>
#include <sstream>
#include <math.h>
/**
 * Necesario para el manejo de threads...
 */
#include <pthread.h>
/**
 * Conexiones TCP, encabezados, etc...
 */
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
/**
 * Manejo de errores, señales e interfaz con el SO
 */
#include <signal.h>
#include <execinfo.h>
#include <stdexcept>
#include <errno.h>
/**
 * Librerías propias y privadas...
 */
#include "../iolib/IOCoreFuncs.h"
#include "../iolib/ioconfigurador.h"
#include "../iolib/IOLogger.h"
/** 
 * MySQL conectores...
 */
#include <mysql_connection.h>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
/*
 * Criptografía
 */
#include <openssl/sha.h>
#include <bitset>

struct archivo {
	FILE * ftemp = 0L;
	string nombre;
	string hash;
	string destino;
	string remitente;
	string mensaje;
	char * tmpnombre;
	unsigned char esGrupal;
	void libera() {
		//remove(tmpnombre);
		this->nombre = "";
		this->hash = "";
		this->destino = "";
		this->remitente = "";
		this->tmpnombre = 0L;
		this->esGrupal = 0;
		this->ftemp = 0L;
		this->mensaje = "";
	}
};

//funciones de servidor:
int iniciaEscucha();
int svrConecta(int svr_fd, string *ipcliente);
int envia(int fd, string datos);
int envia(string cliente, string datos);
int envia_ws(const int fd, const string datos);
int envia_ws(const string cliente, const string datos);

/** @brief Envia al socket el contenido del puntero de archivo proporcionado
 * @param fd int descriptor de archivo del socket destino
 * @param tmpnombre char * nombre del archivo temporal
 * @param tamFrag unsigned long Tamaño de los fragmentos a enviar, este tamaño se suele establecer según la fragmentación recibida.
 * @return La cantidad de bytes totales enviados. -1 en caso de error
 */
long envia_wsbin(const int fd, char * tmpnombre, const unsigned long tamFrag);

/** @brief Busca el usuario por identificador y le envia el contenido del puntero de archivo proporcionado. Esta función mandará llamar subsecuentemente a envia_wsbin(int, char *, long)
 * @param cliente string identificador del cliente destino
 * @param tmpnombre char * nombre del archivo temporal
 * @param tamFrag unsigned long Tamaño de los fragmentos a enviar, este tamaño se suele establecer según la fragmentación recibida.
 * @return La cantidad de bytes totales enviados. -1 en caso de error
 */
int envia_wsbin(const string cliente, char * tmpnombre, const unsigned long tamFrag);

/** @brief Busca un usuario en la lista de usuarios y envía el adjunto contenido en la base de datos. Establecerá la fragmentación predeterminada a 100 KiB (1024 * 100) en caso de que los archivos adjuntos sean mayores a esa cantida de bytes.
 * @param cliente string identificador del cliente destino
 * @param idadjunto string nombre del archivo temporal
 * @return La cantidad de bytes totales enviados. -1 en caso de error
 */
long envia_wsbin(const string cliente, string idadjunto);

/** @brief Prepara un espacio de memoria para enviar información y devuelve un buffer listo con los encabezados necesarios para ser puestos en el socket. El resto de la información debe ser llenado en el espacio vacío.
 * @param largoDatos const unsigned longitud de los datos a alojar en el buffer
 * @param tipo const unsigned tipo de datos a enviar, según el estándar de la RFC 6455. Solamente se usarán los últimos cuatro bits de este valor
 * @param tam unsigned * puntero a un entero sin signo donde se devolverá el tamaño total del buffer creado.
 * @param esFin bool Marca si es un paquete final
 * @return char* devuelve un apuntador al buffer creado.
 */
char* creaBuffer(const unsigned long largoDatos, const unsigned tipo, unsigned *tam, bool esFin);

/** @brief Graba un mensaje enviado a la base de datos
 * @param idorigen const unsigned longitud de los datos a alojar en el buffer
 * @param iddestino const unsigned tipo de datos a enviar, según el estándar de la RFC 6455. Solamente se usarán los últimos cuatro bits de este valor
 * @param mensaje unsigned * puntero a un entero sin signo donde se devolverá el tamaño total del buffer creado.
 * @return string el identificador del mensaje guardado en la base de datos
 */
string grabaMensaje(const string idorigen, const string iddestino, const string mensaje);

/** @brief Ejecuta una consulta SQL a la base de datos usando la conexión activa del servidor.
 * @param query Consulta a ejecutar
 * @return int Número de registros afectados, negativo en caso de error;
 */
int ejecutaSQL(string query);

/** @brief Realiza una consulta a la base de datos y devuelve el resultset con la información.
 * @param query string consulta a realizar, aplican consultas de select;
 * @return sql::ResultSet un apuntaador a resultset con la información cargada o NULL en caso de error
 */
sql::ResultSet * seleccionaDB(string query);
int nextFree();

/** @brief Imprime al stdout en binario las /len/ primeras posiciones de la sección de memoria indicada por /buf/.
 * @param buf * const char apuntador a un arreglo posición de memoria
 * @param len const unsigned posiciones a imprimir
 */
void imprimeBufBinario(const char * buf, const unsigned int len);
/** @brief Levanta la conexión a la base de datos. Los parámetros de conexión son leídos desde el archivo de configuración.
 * @return bool verdadero si es exitoso, de otra forma regresa falso
 */
bool conectaDB();
void *svrLector(void *arg);
void *santo(void *arg);		//Thread para mantener vivos los sockets...
void cicloPrincipal(int svr_fd);
void bestias(int elFD);

/** @brief Le quita la máscara de encriptación a un paquete recibido y que esté enmascarado.
 * @param buffer const unsigned char * contenido de datos original.
 * @param mascara const unsigned char * máscara (arreglo de 4 bytes) que se aplicó
 * @param tam const unsigned long tamaño del buffer
 * @param maskOffset es el desplazamiento en bytes de la máscara en caso de que los bytes recibodos % 4 sea diferente de 0
 * @return char* apuntador al buffer de salida
 */
unsigned char* quitaMascara ( const unsigned char* buffer, std::bitset< 8 >* mascara, const long unsigned int tam, unsigned * maskOffset);
void liberaTodos();

/** @brief Valida que una cifra de comprobación de un archivo corresponda a un archivo nuevo. La cifra de comprobación será el resultado de la función SHA1 del archivo en cuestión.
 * @param chk string la cifra de comprobación (checksum) a validar
 * @return string el ID del adjunto en caso de duplicado, cadena vacía en caso de que no sea duplicado
 */
string validaChecksum(const string chk);

/** @brief Carga un adjunto a la base de datos.
 * @param archivo string el nombre del archivo que se va a usar
 * @param id string el identificador del mensaje
 * @param hash string checksum en SHA1 del archivo
 * @return string El identificador del adjunto en la base de datos
 */
string cargaAdjunto(char * archivo, string id, string hash);

/** @brief Invierte el orden de los bytes (Endian) para que sea compatible con el protocolo de websockets.
 * @param buf *char Apuntador al buffer donde se deben de invertir los bytes
 * @param posInicial const unsigned posición inicial desde donde se hace el proceso
 * @param numBytes const unsigned número de bytes que se invertirán de orden
 */
void flipEndian(char *buf, const unsigned posInicial, const unsigned numBytes);

/** @brief Se encarga de manejar las señales del sistema operativo. Esta función es llamada automáticamente cuando se recibe la señal
 * @param numSignal El número de señal recibido desde el sistema operativo
 */
void manejaSignals(int numSignal);

/** @brief Termina todos los posibles descriptres de archivos (sockets) abiertos al salir abrúptamente de la aldea (por culpa de la escacés de rinocerontes)
 */
void cierraArchivos();
//Constantes de servidor:
const int EN_ESPERA = 5;	//Conexiones que pueden estar esperando a conectar
const int MAX_MENSAJE = 1500;	//Tamaño máximo del mensaje...

