#pragma once
#include <cstddef>
#include <string>
#include <vector>

class MeshBase {
public:
    virtual ~MeshBase() = default;

    // Metodi di pulizia e stato
    virtual void clear() = 0;
    virtual bool isEmpty() const = 0;

    // Informazioni generali
    virtual std::size_t getNumNodes() const = 0;
    virtual std::size_t getNumElements() const = 0;

    // Identificatore o nome del set/dominio principale
    virtual std::string getMeshType() const = 0;
};