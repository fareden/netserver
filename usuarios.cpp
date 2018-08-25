#include "usuarios.h"
#include <list>

using namespace std;

void Usuarios::agregaUsuario(Usuarios::cliente * elnuevo) {
	this->usuarios.push_back(elnuevo);
}

int Usuarios::existeUsuario(const string usuario) {
	//Recordemos que hay que regresar -1 si no está
	int retval = -1;
	for (unsigned i = 0; i < usuarios.size(); i++) {
		if (usuarios[i]->nombre == usuario) {
			retval = i;
			break;
		}
	}
	return retval;
}
Usuarios::cliente * Usuarios::getCliente(const unsigned int id) {
	cliente *retval = NULL;
	try {
		for (unsigned i = 0; i < usuarios.size(); i++) {
			if (usuarios[i]->cliente_fd == id)
				retval = usuarios[i];
		}
	} catch (exception ex) {
		char err[100];
		sprintf(err, "No encontró usuario %d \n", id);
		milog->println(string(err), 2); 
	}
	return retval;
}

Usuarios::cliente * Usuarios::getCliente(const string idUsr) {
	cliente *retval = NULL;
	for (unsigned i = 0; i < usuarios.size(); i++) {
		if (usuarios[i]->idUsuario == idUsr) {
			retval = usuarios[i];
			break;
		}
	}
	return retval;
}
/*
string Usuarios::getListaUsuarios() {
	string retval = "index\x3 descriptor\x3ipcliente\x3tipousuario\x3idsitio\x4";
	unsigned arrSz = lista.size() * 110;
	char cadena[arrSz];
	unsigned i;
	for (i = 0; i < this->lista.size(); i++) {
		sprintf(cadena, "%i\x3%s\x3%s\x3%s\x3%s\x4", lista[i].id, lista[i].turnado.c_str(), lista[i].ipcliente.c_str(), lista[i].tipousuario.c_str(), lista[i].base.c_str());
		retval += string(cadena);
		//cadena << lista[i].id << "\x3" << lista[i].turnado << "\x3" << lista[i].ipcliente << "\x3" << lista[i].tipousuario << "\x3" << lista[i].base << "\x4";
	}
	//retval += cadena.str();
	return retval;
}
*/
unsigned int Usuarios::numClientes() {
	return usuarios.size();
}

void Usuarios::borraUsuario(const unsigned int id) {
	for (unsigned i = 0; i < usuarios.size(); i++) {
		if (usuarios[i]->cliente_fd == id) {
			usuarios.erase(usuarios.begin() + i);
		}
	}
}

void Usuarios::borraUsuarioFD(const unsigned int fd) {
	for (unsigned i = 0; i < usuarios.size(); i++) {
		if (usuarios[i]->cliente_fd == fd) {
			usuarios.erase(usuarios.begin() + i);
		}
	}
}

string Usuarios::getUsuarios() {
	stringstream retval;
	for (unsigned i = 0; i < this->usuarios.size(); i++) {
		retval << usuarios[i]->idUsuario << "\3" << usuarios[i]->nombre << "\4";
	}
	return retval.str();
}

string Usuarios::getUsuariosHR() {
	stringstream retval;
	retval << std::string(73, '-') << "\n" << setw(7) << "id |" << setw(7) << "FD |" << setw(17) << " ID dispositivo|\n";
	retval << std::string(73, '-') << "\n";
	unsigned i;
	for (i = 0; i < this->usuarios.size(); i++) {
		retval << setw(5) << usuarios[i]->cliente_fd << " |" << setw(5) << usuarios[i]->idUsuario << " |" << setw(15) << (usuarios[i]->nombre == "" ? "(anonimo)" : usuarios[i]->nombre) << " |\n";
	}
	retval << std::string(73, '-') << "\n*** Fin del listado ***\n";
	return retval.str();
}
/*
vector< Usuarios::cliente > Usuarios::getClientesSitio(const string sitio)
{
	vector< cliente > retval;
	for (unsigned i = 0; i < lista.size(); i++) {
		if (lista[i].base == sitio) {
			retval.push_back(lista[i]);
		}
	}
	return retval;
}
*/
Usuarios::cliente* Usuarios::at(const unsigned int pos) {
	cliente * retval = 0L;
	if (pos < usuarios.size())
		retval = usuarios.at(pos);
	
	return retval;
}

void Usuarios::setLogger(IOCore::IOLogger* elLogger) {
	milog = elLogger;
}
