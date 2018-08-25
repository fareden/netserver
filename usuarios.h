#include<iostream>
#include<vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "../iolib/IOCoreFuncs.h"
#include "../iolib/IOLogger.h"

class Usuarios {
public:
	struct cliente {
		unsigned	cliente_fd;
		string		idUsuario;
		string		nombre;
		string		ip;
		string		tipoDispositivo;
	};
	
	struct grupo {
		string		id;
		string		nombre;
		vector< cliente >	miembros;
	};

	void agregaUsuario(cliente* elnuevo);
	void borraUsuarioFD(const unsigned fd);
	void borraUsuario(const unsigned id);
	void setLogger(IOCore::IOLogger* elLogger);

	unsigned numClientes();
	int existeUsuario(const string id_dev);
	/**
	 * @brief Devuelve un apuntador a la estructura de cliente que tenga ese descriptor de archivo
	 * @param id Entero, Descriptor de archivo buscando
	*/
	cliente* getCliente(const unsigned id);	
	/**
	 * @brief Devuelve un apuntador a la estructura de cliente según su Identificador arbitrario en tabla
	 * Ese identificador será el que se haya establecido desde la petición GET con el parámetro id
	 * @param dev_id string, identificador arbitrario desde la red
	*/
	cliente* getCliente(const string dev_id);
	cliente* at(const unsigned pos);
	/**
	 * @brief Imprime la lista de usuarios codificada. Las columnas están delimitadas por 0x03 y las filas por 0x04
	 */
	string getUsuarios();
	/**
	 * @brief Imprimie la lista de usuarios en formato legible al humano, muy útil para depuración
	 */
	string getUsuariosHR();

private:
	vector< cliente* >	usuarios;
	vector< grupo* >	grupos;
	IOCore::IOLogger	* milog;
};
