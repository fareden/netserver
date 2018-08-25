#include "usuarios.h"
#include "iosvrmain.h"
#include <fcntl.h>

using namespace std;
using namespace IOCore;
using namespace sql;

//Variables de servidor, que se pueden ajustar desde la configuración:
string puerto = "5000";
unsigned maxUsuarios = 200;
unsigned conectados = 0;
unsigned maxFD = 3;
string prefijo = "";
unsigned tcpverifica = 10;
bool fifando = true;
bool verboso = false;
bool pingea = true;
bool usaValidacion = true;

//Variables para los Threads...
volatile fd_set the_state;

pthread_mutex_t mutex_state = PTHREAD_MUTEX_INITIALIZER;

//Fareden Hacks y estructuras:
IOConfigurador	* miconf;
IOLogger		* milog;
Usuarios		losUsers;
//Base de datos
Connection		* dbcon;
Statement		* stmt;

//pthread_mutex_t boardmutex = PTHREAD_MUTEX_INITIALIZER; // mutex locker for the chessboard vector.
int main(int argc, char **argv) {
	//Iniciando los interceptores de señal...
	signal(SIGSEGV, manejaSignals);
	signal(SIGTERM, manejaSignals);
	signal(SIGABRT, manejaSignals);
	signal(SIGINT, manejaSignals);
//	signal(SIGKILL, manejaSignals);
	string arConf = "/etc/ionetserver.conf";
	for (unsigned ar = 1; ar < argc; ar++) {
		switch(argv[ar][1]) {
			case 'c':
				//Es la configuración...
				arConf = argv[++ar];
				break;
			case 'v':
				cout << "Activando modo platicador...\n";
				verboso = true;
				break;
			case 'h':
				cout << "Modo de uso: ionetserver -v -c [archivo de configuración]\n";
				cout << "\t-v: activa modo expresivo en stdout\n\t-c [archivo de configuración]: usa un archivo de configuración distinto a /etc/ionetserver.conf\n";
				return 0;
				break;
		}
	}
	miconf = new IOConfigurador(arConf);
	puerto = miconf->leeValor("puerto");
	maxUsuarios = atoi(miconf->leeValor("usuarios").c_str());
	prefijo = miconf->leeValor("prefijo");
	milog = new IOLogger("ionetserver", miconf->leeValor("rutalog"), 3000, 5, atoi(miconf->leeValor("nivel").c_str()));
	milog->iniciaLog();
	milog->setVerbosidad(verboso);
	usaValidacion = (miconf->leeValor("modoseguro") == "1");

	string reporte = usaValidacion ? "Habilitada la validación de los usuarios" : "Usando modo plano";
	if (verboso) cout << reporte << "\n";
	milog->println(string(reporte), 1);
	
	int svr_fd = iniciaEscucha();
	if (svr_fd == -1) {
		milog->println("Error iniciando, sale inesperadamente...", 2);
		cerr << "No pudo iniciar en el puerto " << puerto << "\n";
		return 1;
	} else {
		if (conectaDB()) {
			//Iniciar aquí el thread de verificación...
			/*
			if (pingea = (miconf->leeValor("pings") == "1")) {
				pthread_t	elSanx;
				void 		* arg = (void *)1;
				pthread_create(&elSanx, NULL, santo, arg);
			}
			*/
			cicloPrincipal(svr_fd);
		}
		return 0;
	}
}

bool conectaDB() {
	bool		retval;
	sql::Driver	* ctrl;
	
	try {
		ctrl = get_driver_instance();
		dbcon = ctrl->connect(miconf->leeValor("dbsvr"), miconf->leeValor("dbusr"), miconf->leeValor("dbpwd"));
		dbcon->setSchema(miconf->leeValor("dbnombre"));
		retval = true;
	} catch (sql::SQLException &err) {
		cerr<<"Problemas con las base de datos "<<err.what()<<"\n";
		retval = false;
	}
	return retval;
}


int iniciaEscucha()
{
	struct addrinfo hostinfo, *res;
	int sock_fd;
	int svr_fd;
	int retval = 0;
	//variables de opciones del socket...
	int opt_int = 1;		//SO_REUSEADDR: Activa la opción SO_REUSEADDR
	//int ka = 1;		//SO_KEEPALIVE: Enciende el keepAlive
	timeval snd_to; snd_to.tv_sec = 10;	//SO_SNDTIMEO: Tiempo de espera en los envíos...
	linger lng; lng.l_onoff = 1; lng.l_linger = 0;	//SO_LINGER: Activa la administración del cierre y cierra la conexión en chinga
	//int tcpln2 = 30;	//TCP_LINGER2: Tiempo de timeout para conexiones en cierre...
	//int tcpkidle = 60;	//TCP_KEEPIDLE: Intervalo de inactividad TCP
	//Opciones en tester, que además son acequibles desde configuración.
	//int so_err = 1;		//SO_ERROR: notifica que llegen los errores del socket...
	//int kaint = 60;		//TCP_KEEPINTVL: Intervalo de revisión del keepAlive... Cuando se enciende keepalive es indispensable esta opción...
	
	memset(&hostinfo, 0, sizeof(hostinfo));
	hostinfo.ai_family = AF_UNSPEC;		//Para que jale con IPv4 o v6 indistintamente.
	hostinfo.ai_socktype = SOCK_STREAM;
	hostinfo.ai_flags = AI_PASSIVE;
	//Supongo que si cambiamos el primer parámetro, podemos atarlo a una dirección específica...
	getaddrinfo(NULL, puerto.c_str(), &hostinfo, &res);
	svr_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (svr_fd >= 0) {
		string errsock = miconf->leeValor("erroressocket"),
				kaopt = miconf->leeValor("intervalokeepalive"),
				soka = miconf->leeValor("keepalive"),
				soto = miconf->leeValor("enviotimeout"),
				slin = miconf->leeValor("linger"),
				slin2 = miconf->leeValor("linger2"),
				skepid = miconf->leeValor("inactividadka");
		char reporte[200];
		if (soka == "1") {
			errno = 0;
			opt_int = 1;
			retval += setsockopt(svr_fd, SOL_SOCKET, SO_KEEPALIVE, &opt_int, sizeof(int));
			sprintf(reporte, "Encendió el keepAlive: %s. %s", (retval == 0 ? "correcto" : "error"), (errno != 0 ? strerror(errno) : "Sin problemas"));
			if (verboso) cout << reporte << "\n";
			milog->println(string(reporte), (errno != 0 ? 2 : 4));
			errno = 0;
		}
		if (soto != "" && soto != "0") {
			snd_to.tv_sec = atoi(soto.c_str());
			snd_to.tv_usec = atoi(soto.c_str()) * 10000;
			retval += setsockopt(svr_fd, SOL_SOCKET, SO_SNDTIMEO, &snd_to, sizeof(timeval));
			sprintf(reporte, "Estableció el timeout de envio: %s. %s", (retval == 0 ? "correcto" : "error"), (errno != 0 ? strerror(errno) : "Sin problemas"));
			if (verboso) cout << reporte << "\n";
			milog->println(string(reporte), (errno != 0 ? 2 : 4));
			errno = 0;
		}
		if (slin == "1") {
			opt_int = 1;
			retval += setsockopt(svr_fd, SOL_SOCKET, SO_LINGER, &opt_int, sizeof(linger));
			sprintf(reporte, "Pone el linger: %s. %s", (retval == 0 ? "correcto" : "error"), (errno != 0 ? strerror(errno) : "Sin problemas"));
			if (verboso) cout << reporte << "\n";
			milog->println(string(reporte), (errno != 0 ? 2 : 4));
			errno = 0;
		}
		if (slin2 != "" && slin2 != "0") {
			opt_int = atoi(slin2.c_str());
			retval += setsockopt(svr_fd, SOL_SOCKET, TCP_LINGER2, &opt_int, sizeof(int));
			sprintf(reporte, "Ajusta el linger2: %s. %s", (retval == 0 ? "correcto" : "error"), (errno != 0 ? strerror(errno) : "Sin problemas"));
			if (verboso) cout << reporte << "\n";
			milog->println(string(reporte), (errno != 0 ? 2 : 4));
			errno = 0;
		}
		if (skepid != "" && skepid != "0") {
			opt_int = atoi(skepid.c_str());
			retval += setsockopt(svr_fd, IPPROTO_TCP, TCP_KEEPIDLE, &opt_int, sizeof(int));
			sprintf(reporte, "Ajusta la inactividad de KeepAlive: %s. %s", (retval == 0 ? "correcto" : "error"), (errno != 0 ? strerror(errno) : "Sin problemas"));
			if (verboso) cout << reporte << "\n";
			milog->println(string(reporte), (errno != 0 ? 2 : 4));
			errno = 0;
		}
// 		if (errsock == "1") {
// 			opt_int = 1;
// 			retval += setsockopt(svr_fd, SOL_SOCKET, SO_ERROR, &opt_int, sizeof(int));
// 			sprintf(reporte, "Prende los errores... %s. %s", (retval == 0 ? "correcto" : "error"), (errno != 0 ? strerror(errno) : "Sin problemas"));
// 			if (verboso) cout << reporte << "\n";
// 			milog->println(string(reporte), (errno != 0 ? 2 : 4));
// 			errno = 0;
// 		}
		if (kaopt != "") {
			opt_int = atoi(kaopt.c_str());
			retval += setsockopt(svr_fd, IPPROTO_TCP, TCP_KEEPINTVL, &opt_int, sizeof(int));
			sprintf(reporte, "Ajusta el intervalo keepAlive: %s. %s", (retval == 0 ? "correcto" : "error"), (errno != 0 ? strerror(errno) : "Sin problemas"));
			if (verboso) cout << reporte << "\n";
			milog->println(string(reporte), (errno != 0 ? 2 : 4));
			errno = 0;
		}
		//Esta es la opción que venía originalmente en el ejemplo...
		opt_int = 1;
		retval += setsockopt(svr_fd, SOL_SOCKET, SO_REUSEADDR, &opt_int, sizeof(int));
		sprintf(reporte, "Después de establecer las opciones del socket: %s. %s", (retval == 0 ? "correcto" : "error"), (errno != 0 ? strerror(errno) : "Sin problemas"));
		if (verboso) cout << reporte << "\n";
		milog->println(string(reporte), (errno != 0 ? 2 : 4));
		errno = 0;
		if (retval >= 0) {
			retval = bind(svr_fd, res->ai_addr, res->ai_addrlen);
			if (retval == 0) {
				retval = listen(svr_fd, EN_ESPERA);
				if (retval >= 0) {
					retval = svr_fd;
				} else {
					milog->println("Error escuchando...", 2);
				}
			} else {
				milog->println("Error amarrando el puerto...", 2);
			}
		} else {
			milog->println("Error estableciendo las opciones del socket", 2);
		}
	} else {
		milog->println("Error creando el socket", 2);
	}
	return retval;
	
}

int svrConecta(int svr_fd, string *ipcliente) {
	char ipstr[INET6_ADDRSTRLEN];
	string strIP;
	char bufflog[256];
	int port;
	
	int nvoCliente;
	struct sockaddr_storage remote_info ;
	socklen_t addr_size;
	
	addr_size = sizeof(addr_size);
	nvoCliente = accept(svr_fd, (struct sockaddr *) &remote_info, &addr_size);
	if (nvoCliente >= 0) {
		getpeername(nvoCliente, (struct sockaddr*)&remote_info, &addr_size);
		// deal with both IPv4 and IPv6:
		if (remote_info.ss_family == AF_INET) {
			struct sockaddr_in *s = (struct sockaddr_in *)&remote_info;
			port = ntohs(s->sin_port);
			inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
		} else { // AF_INET6
			struct sockaddr_in6 *s = (struct sockaddr_in6 *)&remote_info;
			port = ntohs(s->sin6_port);
			inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
		}
		sprintf(bufflog, "Se conecta el cliente %s desde el puerto %d", ipstr, port);
		milog->println(bufflog, 1);
		strIP = ipstr;
		*ipcliente = strIP;
		return nvoCliente;
	} else {
		char error[100];
		sprintf(error, "Error num. %d aceptando la conexión, saliendo...", errno);
		milog->println(string(error), 2);
		return -1;
	}
}

int envia(int fd, string datos) {
	int retval;
	//datos = "\x5" + datos + "\n";
	retval = send(fd, datos.c_str(), strlen(datos.c_str()),0);
	return retval;
}

int envia(string cliente, string datos) {
	int cliDest = atoi(cliente.c_str());
	int retval ;
	if (cliDest > -1) {
		retval = envia(losUsers.getCliente(atoi(cliente.c_str()))->cliente_fd, datos);
	} else {
		char reporte[100];
		sprintf(reporte, "No puedo enviar el mensaje a un destino negativo: %d", cliDest);
		if (verboso) cout << reporte << "\n";
		milog->println(string(reporte), (errno != 0 ? 2 : 4));
		retval = -1;
	}
	return retval;
}

int envia_ws(const int fd, const string datos) {
	int retval;
	unsigned bufTam;
	char* paq = creaBuffer(datos.length(), 1, &bufTam, true);
	strcat(paq, datos.c_str());
	//cout << "Tamaño codificado: " << bufTam << std::endl;
	retval = send(fd, paq, bufTam, 0);
	return retval;
}

int envia_ws(const string cliente, const string datos) {
	//int cliDest = atoi(cliente.c_str());
	Usuarios::cliente *dest = losUsers.getCliente(cliente);
	int retval;
	if (dest != 0L) {
		retval = envia_ws(dest->cliente_fd, datos);
	} else {
		milog->println("Al parecer, el cliente " + cliente + " no está en línea", (errno != 0 ? 2 : 4));
		retval = -1;
	}
	return retval;
}

long envia_wsbin(const int fd, char * tmpnombre, const unsigned long tamFrag = 0) {
	long restante = 0, retval = 0;
	ssize_t err;
	unsigned bufTam, offset = 0;
	unsigned long tamArch = 0, paqEnvio = 0;
	uint8_t elTipo = 2;
	FILE * archptr = fopen(tmpnombre, "rb");
	tamArch = IOCore::mideArchivo(archptr->_fileno);
	restante = tamArch;
	do {
		paqEnvio = (tamFrag <= tamArch ? tamFrag : restante);
		char *paq = creaBuffer(paqEnvio, elTipo, &bufTam, !(restante > tamFrag));
		char buffArch[paqEnvio];
		err = fread(buffArch, paqEnvio, 1, archptr);
		offset = (bufTam - paqEnvio);
		memcpy(paq + offset, &buffArch, sizeof(buffArch));
		err = send(fd, paq, bufTam, 0);
		if (err < 0) {
			//Ocurrió un error al escribir...
			milog->println("Error de tubería enviando binario", 2);
			retval = -1;
			break;
		} else {
			restante -= err;
			retval += err;
		}
		if (restante > 0) elTipo = 0;
		free(paq);
	} while (restante > 0);
	fclose(archptr);
	return retval;
}

long envia_wsbin(const string cliente, string idadjunto) {
	int cliDest = atoi(cliente.c_str());
	long restante = 0, retval = 0;
	if (cliDest > -1) {
		unsigned offset = 0, bufTam = 0;
		unsigned long tamArch, paqEnvio = 0;
		size_t err = 0;
		istream *bajaImg;
		sql::ResultSet * rs = seleccionaDB("select length(adjunto) as cuenta, adjunto from almacen_binario where id = '" + idadjunto + "';");
		if (rs != 0L && rs->next()) {
			tamArch = rs->getInt64(1);
			ssize_t err;
			restante = tamArch;
			bajaImg = rs->getBlob(2);
			uint8_t elTipo = 2;
			unsigned long tamFrag = (tamArch > (1024 * 100) ? 1024 * 100 : 0);		//Se establece a 0 para desactivar el fragmentado
			do {
				paqEnvio = (tamFrag <= tamArch ? tamFrag : restante);
				char buffArch[paqEnvio];
				bajaImg->read((char*)buffArch, paqEnvio);
				char *paq = creaBuffer(paqEnvio, elTipo, &bufTam, !(restante > tamFrag));
				offset = (bufTam - paqEnvio);
				memcpy(paq + offset, &buffArch, sizeof(buffArch));
				err = send(losUsers.getCliente(cliente)->cliente_fd, paq, bufTam, 0);
				if (err < 0) {
					//Ocurrió un error al escribir...
					milog->println("Error de tubería enviando binario", 2);
					retval = -1;
					break;
				} else {
					restante -= err;
					retval += err;
				}
				if (restante > 0) elTipo = 0;
				free(paq);
			} while (restante > 0);
		}
	} else {
		char reporte[100];
		sprintf(reporte, "No puedo enviar el mensaje a un destino negativo: %d", cliDest);
		if (verboso) cout << reporte << "\n";
		milog->println(string(reporte), (errno != 0 ? 2 : 4));
		retval = -1;
	}
	return retval;
}

int envia_wsbin(const string cliente, char * tmpnombre, const unsigned long tamFrag = 0) {
	int cliDest = atoi(cliente.c_str());
	int retval = 0;
	if (cliDest > -1) {
		retval = envia_wsbin(losUsers.getCliente(cliente)->cliente_fd, tmpnombre, tamFrag);
	} else {
		char reporte[100];
		sprintf(reporte, "No puedo enviar el mensaje a un destino negativo: %d", cliDest);
		if (verboso) cout << reporte << "\n";
		milog->println(string(reporte), (errno != 0 ? 2 : 4));
		retval = -1;
	}
	return retval;
}

char* creaBuffer(const unsigned long largoDatos, const unsigned tipo, unsigned *tam, bool final) {
	char* retval;
	unsigned int bufTam = 2, tamTotal = 0;
	bufTam += (largoDatos > pow(2, 16) ? 8 : (largoDatos > 125 ? 2 : 0));
	tamTotal = largoDatos + bufTam;
	char *buf = (char *)malloc(tamTotal);
	//char buf[tamTotal] {0};
	memset(buf, tamTotal, 0);
	bitset<8> hdr1;
	bitset<8> hdr2;
	hdr1.set(7, final);
	//cout << hdr1.to_string() << std::endl;
	for (unsigned i = 6; i > 3; i--)
		hdr1.set(i, 0);
	hdr1 |= tipo;
	//cout << hdr1.to_string() << std::endl;
	hdr2.set(7, 0);
	if (bufTam == 2) {
		uint8_t ttam = (uint8_t)largoDatos;
		hdr2 |= ttam;
	} else if (bufTam == 4) {
		hdr2 |= 126;
		memcpy(buf + 2, &largoDatos, 2);
		char tmp = buf[2];
		buf[2] = buf[3];
		buf[3] = tmp;
	} else {
		hdr2 |= 127;
		memcpy(buf + 2, &largoDatos, 8);
		//Invertir el orden según adecuado...
		flipEndian(buf, 2, 8);
	}
	buf[0] = (uint8_t)hdr1.to_ulong();
	buf[1] = (uint8_t)hdr2.to_ulong();
	retval = buf;
	*tam = tamTotal;
	return retval;
}

void flipEndian(char *buf, const unsigned posInicial, const unsigned numBytes) {
	char tmp = 0x00;
	for (unsigned i = 0; i < ((numBytes - posInicial) / 2); i++) {
		unsigned posDest = (posInicial + (numBytes - 1)) - i;
		tmp = buf[posDest];
		buf[posDest] = buf[i + posInicial];
		buf[i + posInicial] = tmp;
	}
}
/*********
 * Aquí está el ciclo principal, aquí se procesa toda la información y es un thread independiente...
 * Ejemplo de petición de saludo
GET / HTTP/1.1
Host: localhost:5500
Pragma: no-cache
Cache-Control: no-cache
Origin: https://carpathialab.com
Sec-WebSocket-Version: 13
User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/67.0.3396.87 Safari/537.36
Accept-Encoding: gzip, deflate, br
Accept-Language: es-US,es;q=0.9,es-419;q=0.8,en;q=0.7
Cookie: _ga=GA1.2.1180399066.1527706303; PHPSESSID=60t1mahve92vqfo2udfopp2i17
Sec-WebSocket-Key: hGuC1NwPGp7jjOCd2t6c4w==
Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits
X-Forwarded-For: 187.144.207.59
X-Forwarded-Host: carpathialab.com
X-Forwarded-Server: carpathialab.com
Upgrade: WebSocket
Connection: Upgrade
 */
void* svrLector(void* arg) {
	unsigned char buf[MAX_MENSAJE] {0};
	unsigned rfd = (long)arg;
	Usuarios::cliente *miCliente = losUsers.getCliente(rfd);
	archivo * miArch = new archivo();
	int buflen;
	bool fifando = true, pendiente = true, platicando = false;
	string mensaje = "";
	while(fifando) {
		if (!platicando) {
			do {
				buflen = read(rfd, buf, sizeof(buf));
				//pthread_mutex_lock(&mutex_state);
				if (buflen > 0) {
					for (unsigned c = 0; c < buflen; c++) {
						mensaje += buf[c];
						if (buf[c] == '\n') {
							pendiente = !IOCore::strTerminaCon(mensaje, "\r\n\r\n");
							if (!pendiente) break;
						}
					}
				} else {
					//Pausamos un ratito
					//pthread_mutex_unlock(&mutex_state);
					sleep(2);
				}
			} while (pendiente);
			if (mensaje != "") {
				milog->println("Mensaje recibido: " + mensaje, 1);
				vector< string > lineas = IOCore::split(mensaje, "\r\n");
				string llave = "";
				//Por lo pronto solamente vamos a la petición de chat
				if (lineas[0].substr(0, 6) == "GET /?") {
					for (unsigned i = 1; i < lineas.size(); i++) {
						if (lineas[i] != "") {
							vector< string > par = IOCore::split(lineas[i], ": ", 2);
							if (par[0] == "Sec-WebSocket-Key") llave = par[1];
							//TODO detectar otros valores como host, etc.
							if (par[0] == "X-Forwarded-For") miCliente->ip = par[1];
						}
					}
					//Calculamos la nueva llave:
					//llave + 258EAFA5-E914-47DA-95CA-C5AB0DC85B11 > sha1 > base64
					llave += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
					unsigned char tmp[20];
					memset(tmp, 0x0, 20);
					SHA1((unsigned char*)llave.c_str(), llave.length(), tmp);
					llave = IOCore::base64_encode(tmp, 20);
					//Enviamos el saludo de regreso...
					envia(rfd, "HTTP/1.1 101 Web Socket Protocol Handshake Switching Protocols\r\n\
Access-Control-Allow-Credentials: true\r\n\
Access-Control-Allow-Headers: content-type\r\n\
Access-Control-Allow-Headers: authorization\r\n\
Access-Control-Allow-Headers: x-websocket-extensions\r\n\
Access-Control-Allow-Headers: x-websocket-version\r\n\
Access-Control-Allow-Headers: x-websocket-protocol\r\n\
Access-Control-Allow-Origin: http://localhost\r\n\
Connection: Upgrade\r\n\
Sec-WebSocket-Accept: " + llave + "\r\n\
Server: Ozomahtli\r\n\
Upgrade: websocket\r\n\r\n");
					milog->println("... saludó con llave: " + llave, 1);
					string pet = lineas[0].substr(6);
					pet = pet.substr(0, pet.length() - 7);
					//Procesamos la petición
					vector< string > arrPet = IOCore::split(pet, "&");
					for (unsigned p = 0; p < arrPet.size(); p++) {
						vector< string > prt = IOCore::split(arrPet[p], "=");
						if (prt[0] == "id") miCliente->idUsuario = prt[1];
						if (prt[0] == "nombre") miCliente->nombre = prt[1];
					}
					platicando = true;
					mensaje = "";
				} else if (lineas[0] == "TIAHUI") {
					//modo de saludo de depuración...
					
				} else {
					//Cerramos la conexión abrúptamente, no es amigo
					fifando = false;
				}
			} else {
				sleep(2);
			}
		} else {
			//Imprimimos el valor en binario:
			unsigned tipo = 0;
			bool esFin = false, hayFrag = false;
			long tamFrag = 0;
			//Encabezado, validamos que tengamos bytes suficientes:
			do {
				long carga = 0, leido = 0;
				uint8_t tCarga = 0;
				ssize_t err = 0;
				unsigned int offset = 0;
				bitset<8> mascara[4];
				err = read(rfd, buf, 2);
				//pthread_mutex_lock(&mutex_state);
				if (err == 2) {
					bitset<8> hdr1(buf[0]);
					bitset<8> hdr2(buf[1]);
					esFin = hdr1[7];
					unsigned tipotmp = (bitset<8>(hdr1<<4)>>4).to_ulong();
					tipo = (hayFrag ? tipo : tipotmp);
					if (esFin && !hayFrag) {
						//Validamos que sea ceros los RSV
						if ((hdr1[6] || hdr1[5] || hdr1[4])) {
							fifando = false;
							break;
						}
					} else if (esFin && hayFrag && tipotmp == 0) {
						//TODO contabilizar los reserved
						//cout << "Es el fin de un paquete fragmentado: " << hdr1.to_string() << std::endl;
						hayFrag = false;
					} else if (esFin && hayFrag && tipotmp != 0) {
						cout << "Me llega un tipo raro de control... " << hdr1.to_string() << std::endl;
						esFin = false;
						continue;
					} else {
						hayFrag = true;
					}
					//Revisamos el tipo
					if (hdr2[7]) {
						//Los clientes siempre deben enviar este bit prendido
						tCarga = (bitset<8>(hdr2<<1)>>1).to_ulong();
						switch (tCarga) {
							case 126:
								err = read(rfd, buf, 2);
								if (err == 2) {
									bitset<16> tam = buf[0] << 8 | buf[1];
									carga = (unsigned int)tam.to_ulong();
								} else {
									//TODO error de lectura
								}
								break;
							case 127:
								//TODO sumar los siguientes 8 bytes...
								err = read(rfd, buf, 8);
								if (err == 8) {
									bitset<64>tam2;
									for (unsigned i = 0; i < 8; i++) {
										tam2 |= ((uint64_t)buf[i]) << ((8 - (i+1)) * 8);
									}
									carga = (long)tam2.to_ulong();
								} else {
									//TODO error de lectura
								}
								break;
							default:
								carga = tCarga;
								break;
						}
						if (hayFrag && tamFrag == 0) tamFrag = carga;
					} else {
						fifando = false;
						break;
					}
				}
				//Leemos la máscara...
				err = read(rfd, buf, 4);
				if (err == 4) {
					for (unsigned m = 0; m < 4; m++) {
						mascara[m] = bitset<8>(buf[m]);
					}
				}
				//Tenemos el tamaño de la carga, vamos a leer ese número de bytes.
				unsigned desplMascara = 0;
				//pthread_mutex_unlock(&mutex_state);
				do {
					err = read(rfd, buf, (carga - leido));
					//pthread_mutex_lock(&mutex_state);
					leido += err;
					//cout << "Leidos " << leido << " bytes de los " << carga << " esperados en un paquete de " << err << " bytes" << std::endl;
					//Leyó los bytes pendientes correctamente...
					switch (tipo) {
						case 8:		//Es un error
							fifando = false;
						case 3: case 4: case 5: case 6: case 7:
							cout << "Mensaje de control tipo: " << tipo << " recibido. Tamaño del buffer: " << err << std::endl;
						case 1:		//Es un mensaje de texto
							for (unsigned c = 0; c < err; c++) {
								bitset<8> bs(buf[c]);
								mensaje += (char)(bs^mascara[(c - offset) % 4]).to_ulong();
							}
							milog->println("Mensaje obtenido: " + mensaje, 1);
							break;
						case 2:		//Creo que es un binario
							//No aceptaremos un envío de binario salvo que se haya solicitado previamente vía el protoloco de texto
							if (miArch->ftemp != 0L) {
								//Escribir buffer a archivo...
								//Desenmascarar el contenido... bronca en el byte 1116
								unsigned char * buffClaro = quitaMascara(buf, mascara, err, &desplMascara);
								err = fwrite(buffClaro, err, 1, miArch->ftemp);
								free(buffClaro);
								//write(miArch->fd_temp, buf, err);
							} else {
								//Archivo sin anunciar, notificamos y sacamos del servidor
								milog->println("Se intentó mandar un archivo sin aviso, salimos...", 2);
								fifando = false;
							}
							break;
						case 9:
							milog->println("Ping recibido del cliente: " + miCliente->ip, 1);
							break;
						case 10:
							milog->println("Pong recibido: " + miCliente->ip, 1);
							break;
					}
					if (leido == carga && !hayFrag) {
						if (tipo == 1) {
							//vamos por la mensajería de texto, también aquí nos vamos a apoyar para la mensajería binaria
							/*
							 * Aquí se inserta el protocolo para procesar texto plano
							 */
						} else if (tipo == 2) {
							//Enviamos notificación del archivo binario al cliente
							if (miArch->ftemp != 0L) {
								if (miArch->esGrupal == 0) {
									fclose(miArch->ftemp);
									/*
									 * Aquí se inserta el protocolo para administración de los binarios. Se deja la llamada al envío de binarios
									 * 
									 */
									envia_wsbin(miArch->destino, miArch->tmpnombre, tamFrag);
									miArch->libera();
									//pthread_mutex_lock(&mutex_state);
								} else {
									//TODO enviar a grupos
								}
								
							}
						}
					} else if (leido == carga && hayFrag) {
						//Qué vamos a hacer con los pendientes?
						switch (tipo) {
							case 1:
								mensaje += mensaje;
								break;
							case 2:
								cout << "Fin de carga fragmentada de " << carga << " bytes\n";
								break;
							default:
								cout << "Caso inesperado\n";
								break;
						}
					}
					//desplMascara = err % 4;
					//pthread_mutex_unlock(&mutex_state);
				} while (leido < carga);
			} while (!esFin);
		}
		usleep(500 * 1000);
	}
	conectados = conectados - 1;
	if (verboso) cout << "... saliendo, conectados: " << conectados << "\n";
	bestias(rfd);
	return NULL;
}

string creanombretmp() {
	string retval;
	
	return retval;
}

void cicloPrincipal(int svr_fd) {
	pthread_t threads[maxUsuarios]; //create 10 handles for threads.
	FD_ZERO(&the_state); // FD_ZERO clears all the filedescriptors in the file descriptor set fds.
	
	while(fifando) {
		int rfd = 0;
		//void *arg;
		string ipcliente;
		
		// if a client is trying to connect, establish the connection and create a fd for the client.
		rfd = svrConecta(svr_fd, &ipcliente);
		if (rfd >= 0) {
			milog->println("Se conecta un cliente nuevo...", 1);
			if (conectados > maxUsuarios) {
				milog->println("El servidor está lleno, abortando la conexión...", 2);
				close(rfd);
			} else {
				Usuarios::cliente * elnuevo = new Usuarios::cliente;
				maxFD = (rfd > maxFD ? rfd : maxFD);
				pthread_mutex_lock(&mutex_state);  // Make sure no 2 threads can create a fd simultanious.
				FD_SET(rfd, &the_state);  // Add a file descriptor to the FD-set.
				elnuevo->cliente_fd = rfd;
				losUsers.agregaUsuario(elnuevo);
				conectados = losUsers.numClientes();
				if (verboso) cout  << "Cliente nuevo, conectados: " << conectados << ", descriptor de archivo: " << rfd << "\n";
				pthread_create(&threads[++conectados], NULL, svrLector, (void *) rfd);
				pthread_mutex_unlock(&mutex_state); // End the mutex lock
			}
		}
		usleep(750 * 1000);
	}
}

string validaChecksum(const string chk) {
	string retval = "";
	string qry = "select id from almacen_binario where checksum = '" + chk + "';";
	sql::ResultSet * rs = seleccionaDB(qry);
	if (rs != nullptr) {
		if (rs->next())
			retval = rs->getString(1);
	}
	return retval;
}

int nextFree() {
	int retval = -1;
	for (unsigned i = 0; i < maxUsuarios; i++) {
		if (losUsers.getCliente(i) == NULL) {
			retval = i;
			break;
		}
	}
	return retval;
}

int ejecutaSQL(string query) {
	int retval;
	try {
		stmt = dbcon->createStatement();
		retval = stmt->executeUpdate(query);
		delete stmt;
	} catch (sql::SQLException &err) {
		milog->println("Ejecutando: " + query, 2);
		milog->println(err.what(), 2);
		retval = -1;
	} catch (...) {
		milog->println("otro error ejecutando: " + query, 2);
		retval = -1;
	}
	return retval;
}

void cierraArchivos() {
	for (unsigned i = 3; i < maxFD; i++) {
		FD_CLR(i, &the_state);
		close(i);
	}
}

void manejaSignals(int numSignal) {
	switch (numSignal) {
		case SIGTERM: {
			milog->println("*** Señal de terminación recibida!! ***", 1);
			milog->terminaLog();
			cierraArchivos();
			exit(numSignal);
		}
		break;
		case SIGINT: {
			char sale;
			cout << "¿Realmente desea salir? terminará todas las conexiones activas... (S/n): "; cin >> sale;
			if (sale == 's' || sale == 'S') {
				milog->println("*** Terminado por el usuario usando Control + C ***", 1);
				milog->terminaLog();
				cierraArchivos();
				exit(numSignal);
			} else {
				milog->println("Salida cancelada...", 4);
				cout << "... seguimos trabajando, pues...\n";
			}
		} break;
		case SIGKILL: {
			milog->println("*** Proceso matado desde el OS ***", 1);
			milog->terminaLog();
			cierraArchivos();
			exit(numSignal);
		} break;
		case SIGABRT:
		case SIGSEGV: {
			void	*arreglo[10];
			size_t	tam;
			
			tam = backtrace(arreglo, 10);
			
			char **traza = backtrace_symbols(arreglo, tam);
			milog->println("** Recibida la señal " + string(strsignal(numSignal)) + " **", 2);
			for (unsigned i = 0; i < tam; i++) {
				milog->println(traza[i], 2);
			}
			milog->terminaLog();
			cierraArchivos();
			exit(numSignal);
		}
		break;
	}
	return;
}

unsigned char* quitaMascara (const unsigned char* buffer, std::bitset< 8 >* mascara, const long unsigned int tam, unsigned * maskOffset) {
	unsigned char * retval = (unsigned char *)malloc(tam);
	//memset(retval, 0x00, tam);
	for (unsigned i = 0; i < tam; i++) {
		retval[i] = (char)buffer[i]^mascara[*maskOffset].to_ulong();
		*maskOffset = (*maskOffset < 3 ? *maskOffset + 1 : 0);
	}
	return retval;
}
void bestias(int elFD) {
	//Borrar el dato de la tabla de usuarios...
	cout << "Usuarios antes de borrar: " << losUsers.numClientes() << std::endl;
	losUsers.borraUsuarioFD(elFD);
	cout << "Usuarios DESPUES de borrar: " << losUsers.numClientes() << std::endl;
	FD_CLR(elFD, &the_state);      // free fd's from  clients
	close(elFD);
	//Reenvíar la tabla de usuarios
}

void imprimeBufBinario(const char* buf, const unsigned int len) {
	cout << "0: ";
	for (unsigned c = 0; c < len; c++) {
		bitset<8> bs(buf[c]);
		cout << bs << " ";
		if ((c + 1) % 4 == 0)
			cout << std::endl << c << ": ";
	}
	cout << "\n************\n";
}

sql::ResultSet * seleccionaDB(string query) {
	sql::ResultSet * retVal;
	Statement *stmt;
	try {
		if ( dbcon != nullptr) {
			stmt = dbcon->createStatement();
			//stmt->setQueryTimeout(120);
			retVal = stmt->executeQuery(query);
		} else {
			milog->println("Ejecutando: " + query, 3);
			milog->println("La conexion a la base de datos es nula...", 4);
			retVal = nullptr;
		}
	} catch (SQLException &sqlex) {
		milog->println("Ejecutando: " + query, 3);
		milog->println(sqlex.what(), 2);
	} catch (...) {
		milog->println("Error inesperado en el select...", 2);
	}
	return retVal;
}

string cargaAdjunto(char * archivo, string id, string hash) {
	std::ifstream lafoto;
	lafoto.open(archivo);
	if (lafoto.good()) {
		try {
			string qry = "insert into almacen_binario (idorigen, checksum, adjunto, adjunto_mime) values ("+id+", '"+hash+"', ?, 'application/octet-stream');";
			//sql::PreparedStatement * ins = new sql::PreparedStatement();
			sql::PreparedStatement * insertador = dbcon->prepareStatement(qry);
			insertador->setBlob(1, &lafoto);
			insertador->executeUpdate();
			qry = "select id from almacen_binario where checksum in ('"+hash+"');";
			sql::ResultSet * rs = seleccionaDB(qry);
			if (rs != 0L && rs->next())  {
				return rs->getString(1);
			}
		} catch (sql::SQLException sex) {
			milog->println("Cargando el archivo: " + (string)archivo, 2);
			milog->println(sex.what(), 2);
		}
		lafoto.close();
	}
	return "";
}

string grabaMensaje(const string idorigen, const string iddestino, const string mensaje) {
	string retval = "";
	string qry = "select enviaMensaje('"+idorigen+"', '"+iddestino+"', '"+mensaje+"');";
	sql::ResultSet * rs = seleccionaDB(qry);
	if (rs != 0L && rs->next())
		retval = rs->getString(1);
	return retval;
}

