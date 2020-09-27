#pragma once

#include "VirtualMachine/VirtualMachineState.h"
#include "VirtualMachine/VirtualMachinePool.h"
#include "DiscreteDistribution.h"
#include "ProgramLoader.h"

// We define a type for missing arguments -- this can be passed as input (or even output)
// and callable will handle with thunks
//class empty_t {};

/**
 * @class Callable
 * @author Steven Piantadosi
 * @date 03/09/20
 * @file Callable.h
 * @brief A callable function is one that can be called. It must be able to load a program.
 * 			Putting this into an interface allows us to call the object to get output, by compiling
 * 			and running on a VirtualMachine
 */
template<typename input_t, typename output_t, typename VirtualMachineState_t>
class Callable : public ProgramLoader {
public:
	
	Callable() { }

	/**
	 * @brief Can this be evalutaed (really should be named -- can be called?). Sometimes partial hypotheses
	 * 		  can't be called.
	 */
	[[nodiscard]]  virtual bool is_evaluable() const = 0; 
		
	/**
	 * @brief Run the virtual machine on input x, and marginalize over execution paths to return a distribution
	 * 		  on outputs. Note that loader must be a program loader, and that is to handle recursion and 
	 *        other function calls. 
	 * @param x - input
	 * @param err - output value on error
	 * @param loader - where to load recursive calls
	 * @param max_steps - max steps the virtual machine pool will run for
	 * @param max_outputs - max outputs the virtual machine pool will run for
	 * @param minlp - the virtual machine pool doesn't consider paths less than this probability
	 * @return 
	 */	
	virtual DiscreteDistribution<output_t> call(const input_t x, const output_t err=output_t{}, ProgramLoader* loader=nullptr) {
					
		// make this defaulty be the loader
		if(loader == nullptr) 
			loader = this;
		
		VirtualMachinePool<VirtualMachineState_t> pool; 		
		
		VirtualMachineState_t* vms = new VirtualMachineState_t(x, err, loader, &pool);	
		push_program(vms->program); // write my program into vms

		pool.push(vms);		
		return pool.run();				
	}
	
	virtual DiscreteDistribution<output_t>  operator()(const input_t x, const output_t err=output_t{}, ProgramLoader* loader=nullptr){ // just fancy syntax for call
		return call(x,err,loader);
	}

	/**
	 * @brief A variant of call that assumes no stochasticity and therefore outputs only a single value. 
	 * 		  (This uses a nullptr virtual machine pool, so will throw an error on flip)
	 * @param x
	 * @param err
	 * @return 
	 */
	virtual output_t callOne(const input_t x, const output_t err=output_t{}, ProgramLoader* loader=nullptr) {
		
		if(loader == nullptr) 
			loader = this;
		
		// we can use this if we are guaranteed that we don't have a stochastic Hypothesis
		// the savings is that we don't have to create a VirtualMachinePool		
		VirtualMachineState_t vms(x, err, loader, nullptr);		

		push_program(vms.program); // write my program into vms (loader is used for everything else)
		return vms.run(); // default to using "this" as the loader		
	}

	
	/**
	 * @brief A fancy form of calling which returns all the VMS states that completed. The normal call function 
	 * 		  marginalizes out the execution path.
	 * @return a vector of virtual machine states
	 */	
	std::vector<VirtualMachineState_t> call_vms(const input_t x, const output_t err=output_t{}, ProgramLoader* loader=nullptr){
		if(loader == nullptr) 
			loader = this;
			
		VirtualMachinePool<VirtualMachineState_t> pool; 		
		VirtualMachineState_t* vms = new VirtualMachineState_t(x, err, loader, &pool);	
		push_program(vms->program); 
		pool.push(vms);		
		return pool.run_vms();				
	}
	
	
	/**
	 * @brief Call, assuming no stochastics, return the virtual machine instead of the output
	 * @param x
	 * @param err
	 * @return 
	 */
	VirtualMachineState_t callOne_vms(const input_t x, const output_t err=output_t{}, ProgramLoader* loader=nullptr) {
		if(loader == nullptr) 
			loader = this;
			
		VirtualMachineState_t vms(x, err, loader, nullptr);
		push_program(vms.program); // write my program into vms (loader is used for everything else)
		vms.run(); // default to using "this" as the loader		
		return vms;
	}

	
};