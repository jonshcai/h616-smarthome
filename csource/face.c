#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "face.h"

static PyObject *pModule = NULL;

void face_init(void)
{
    Py_Initialize();
    
    PyObject *sys = PyImport_ImportModule("sys");
    PyObject *path = PyObject_GetAttrString(sys, "path");
    PyList_Append(path, PyUnicode_FromString("."));
    
    pModule = PyImport_ImportModule("face");
    if (!pModule) {
        PyErr_Print();
        printf("Error: failed to load face.py\n");
    }
}

void face_final(void)
{
    if (pModule) {
        Py_DECREF(pModule);
    }
    Py_Finalize();
}

double face_category(void)
{
    if (!pModule) {
        return 0.0;
    }
    
    PyObject *pFunc = PyObject_GetAttrString(pModule, "alibaba_face");
    if (!pFunc) {
        PyErr_Print();
        printf("Error: failed to load alibaba_face\n");
        return 0.0;
    }
    
    PyObject *pValue = PyObject_CallObject(pFunc, NULL);
    if (!pValue) {
        PyErr_Print();
        printf("Error: function call failed\n");
        Py_DECREF(pFunc);
        return 0.0;
    }
    
    double result = 0.0;
    if (!PyArg_Parse(pValue, "d", &result)) {
        PyErr_Print();
        printf("Error: parse failed\n");
    }
    
    Py_DECREF(pValue);
    Py_DECREF(pFunc);
    return result;
}