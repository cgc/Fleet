#pragma once 

#include <vector>
#include <assert.h>
#include "IO.h"

/**
 * @class Vector2D
 * @author Steven Piantadosi
 * @date 09/08/20
 * @brief Just a little wrapper to allow vectors to be handled as 2D arrays, which simplifie some stuff in GrammarHypothesis
 */
template<typename T>
struct Vector2D {
	int xsize = 0;
	int ysize = 0;

	std::vector<T> value; 
	
	Vector2D() { }
	
	Vector2D(int x, int y) { 
		resize(x,y);
	}
	
	Vector2D(int x, int y, T b) { 
		resize(x,y);
		fill(b);
	}
	
	void fill(T x){
		std::fill(value.begin(), value.end(), x);
	}
	
	void resize(const int x, const int y) {
		xsize=x; ysize=y;
		value.resize(x*y);
	}
	
	void reserve(const int x, const int y) {
		xsize=x; ysize=y;
		value.reserve(x*y);
	}
	
	const T& at(const int x, const int y) const {
		if constexpr (std::is_same<T,bool>::value) { assert(false && "*** Golly you can't have references in std::vector<bool>."); }
		else { 
			return value.at(x*ysize + y);
		}
	}
	
	T& at(const int x, const int y) {
		if constexpr (std::is_same<T,bool>::value) { assert(false && "*** Golly you can't have references in std::vector<bool>."); }
		else { 
			return value.at(x*ysize + y);
		}
	}
	
	T& operator()(const int x, const int y) const {
		if constexpr (std::is_same<T,bool>::value) { assert(false && "*** Golly you can't have references in std::vector<bool>."); }
		else { 
			return value.at(x*ysize + y);
		}
	}
	
	const T& operator()(const int x, const int y) {
		return value.at(x*ysize + y);
	}
	
	
	// get and Set here are used without references (letting us to 2D vectors of bools)
	void set(const int x, const int y, const T& val) {
		value.at(x*ysize + y) = val; 
	}
	void set(const int x, const int y, const T&& val) {
		value.at(x*ysize + y) = val; 
	}
	T get(const int x, const int y) {
		return value.at(x*ysize+y);
	}
	
	template<typename X>
	void operator[](X x) {
		CERR "**** Cannot use [] with Vector2d, use .at()" ENDL;
		throw YouShouldNotBeHereError();
	}
};

/**
 * @class Vector2D
 * @author Steven Piantadosi
 * @date 03/05/22
 * @file Vector2D.h
 * @brief A little trick here to force bools to act like chars and have normal std::vector iterators etc.
 * 		  This wastes space but prevents us from writing other code. 
 */
//
//template<>
//struct Vector2D<bool> : Vector2D<unsigned char> {
//	using Super = Vector2D<unsigned char>;
//	using Super::Super;
//	
//};
