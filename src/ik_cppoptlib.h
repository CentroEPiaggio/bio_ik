// Bio IK for ROS
// Philipp Ruppel

#pragma once

#include "ik_base.h"

#include "ik_evolution_1.h"

#include "../../CppNumericalSolvers/include/cppoptlib/meta.h"
#include "../../CppNumericalSolvers/include/cppoptlib/problem.h"
#include "../../CppNumericalSolvers/include/cppoptlib/boundedproblem.h"

namespace bio_ik
{

/*
struct IKOptLibProblem : cppoptlib::Problem<double> 
{
    IKBase* ik;
    std::vector<double> fk_values;
    IKOptLibProblem(IKBase* ik) : ik(ik)
    {
    }
    void initialize()
    {
        // set all variables to initial guess, including inactive ones
        fk_values = ik->request.initial_guess;
    }
    double value(const TVector& x) 
    {
        // fill in active variables and compute fitness
        for(size_t i = 0; i < ik->active_variables.size(); i++) fk_values[ik->active_variables[i]] = x[i];
        return ik->computeFitness(fk_values);
    }
    bool callback(const cppoptlib::Criteria<double>& state, const TVector& x)
    {
        // check ik timeout
        return ros::Time::now().toSec() < ik->request.timeout;
    }
};
*/

// problem description for cppoptlib
struct IKOptLibProblem : cppoptlib::BoundedProblem<double> 
{
    IKBase* ik;
    std::vector<double> fk_values;
    IKOptLibProblem(IKBase* ik) : ik(ik), cppoptlib::BoundedProblem<double>(TVector(ik->active_variables.size()), TVector(ik->active_variables.size()))
    {
        // init bounds
        for(size_t i = 0; i < ik->active_variables.size(); i++)
        {
            m_lowerBound[i] = ik->modelInfo.getMin(ik->active_variables[i]);
            m_upperBound[i] = ik->modelInfo.getMax(ik->active_variables[i]);
        }
    }
    void initialize()
    {
        // set all variables to initial guess, including inactive ones
        fk_values = ik->request.initial_guess;
    }
    double value(const TVector& x) 
    {
        // fill in active variables and compute fitness
        for(size_t i = 0; i < ik->active_variables.size(); i++) fk_values[ik->active_variables[i]] = x[i];
        //for(size_t i = 0; i < ik->active_variables.size(); i++) LOG(i, x[i]); LOG("");
        //for(size_t i = 0; i < ik->active_variables.size(); i++) std::cerr << ((void*)*(uint64_t*)&x[i]) << " "; std::cerr << std::endl;
        //size_t h = 0; for(size_t i = 0; i < ik->active_variables.size(); i++) h ^= (std::hash<double>()(x[i]) << i); LOG((void*)h);
        return ik->computeFitness(fk_values);
    }
    bool callback(const cppoptlib::Criteria<double>& state, const TVector& x)
    {
        // check ik timeout
        return ros::Time::now().toSec() < ik->request.timeout;
    }
};


// ik solver using cppoptlib
template<class SOLVER, int reset_if_stuck, int threads>
struct IKOptLib : IKBase
{
    // current solution
    std::vector<double> solution, best_solution, temp;
    
    // cppoptlib solver
    std::shared_ptr<SOLVER> solver;
    
    // cppoptlib problem description
    IKOptLibProblem f;
    
    // reset flag
    bool reset;
    
    // stop criteria
    cppoptlib::Criteria<double> crit;

    IKOptLib(const IKParams& p) : IKBase(p), f(this)
    {
    }
    
    void initialize(const IKRequest& request)
    {
        IKBase::initialize(request);
        
        // set initial guess
        solution = request.initial_guess;
        
        // randomize if more than 1 thread
        reset = false;
        if(thread_index > 0) reset = true;
                
        // init best solution
        best_solution = solution;
        
        // initialize cppoptlib problem description
        f = IKOptLibProblem(this);
        f.initialize();
        
        // init stop criteria (timeout will be handled explicitly)
        crit = cppoptlib::Criteria<double>::defaults();
        //crit.iterations = SIZE_MAX;
        crit.gradNorm = 1e-10;
        //p.node_handle.param("optlib_stop", crit.gradNorm, crit.gradNorm);
        
        if(!solver) solver = std::make_shared<SOLVER>();
    }
    
    const std::vector<double>& getSolution() const
    {
        return best_solution;
    }
    
    void step()
    {
        // set stop criteria
        solver->setStopCriteria(crit);
    
        // random reset if stuck (and if random resets are enabled)
        if(reset)
        {
            //LOG("RESET");
            reset = false;
            for(auto& vi : active_variables)
                solution[vi] = random(modelInfo.getMin(vi), modelInfo.getMax(vi));
        }
    
        // set to current positions
        temp = solution;
        typename SOLVER::TVector x(active_variables.size());
        for(size_t i = 0; i < active_variables.size(); i++) x[i] = temp[active_variables[i]];
        
        // solve
        solver->minimize(f, x);
        
        // get results
        for(size_t i = 0; i < active_variables.size(); i++) temp[active_variables[i]] = x[i];
        
        // update solution
        if(computeFitness(temp) < computeFitness(solution))
        {
            solution = temp;
        }
        else
        {
            if(reset_if_stuck) reset = true;
        }
        
        // update best solution
        if(computeFitness(solution) < computeFitness(best_solution))
        {
            best_solution = solution;
        }
    }
    
    size_t concurrency() const { return threads; }
};

}


static std::string mkoptname(std::string name, int reset, int threads)
{
    name = "optlib_" + name;
    if(reset) name += "_r";
    if(threads > 1) name += "_" + std::to_string(threads);
    return name;
}

#define IKCPPOPTX(n, t, reset, threads) static bio_ik::IKFactory::Class<bio_ik::IKOptLib<cppoptlib::t<bio_ik::IKOptLibProblem>, reset, threads>> ik_optlib_##t##reset##threads(mkoptname(n, reset, threads));

//#define IKCPPOPT(n, t) static bio_ik::IKFactory::Class<bio_ik::IKOptLib<cppoptlib::t<bio_ik::IKOptLibProblem>>> ik_optlib_##t(n)

#define IKCPPOPT(n, t) \
    IKCPPOPTX(n, t, 0, 1) \
    IKCPPOPTX(n, t, 0, 2) \
    IKCPPOPTX(n, t, 0, 4) \
    IKCPPOPTX(n, t, 1, 1) \
    IKCPPOPTX(n, t, 1, 2) \
    IKCPPOPTX(n, t, 1, 4)



#include "../../CppNumericalSolvers/include/cppoptlib/solver/bfgssolver.h"
IKCPPOPT("bfgs", BfgsSolver);

//#include "../../CppNumericalSolvers/include/cppoptlib/solver/cmaesbsolver.h"
//IKCPPOPT("cmaesb", CMAesBSolver);

//#include "../../CppNumericalSolvers/include/cppoptlib/solver/cmaessolver.h"
//IKCPPOPT("cmaes", CMAesSolver);

#include "../../CppNumericalSolvers/include/cppoptlib/solver/conjugatedgradientdescentsolver.h"
IKCPPOPT("cgd", ConjugatedGradientDescentSolver);

#include "../../CppNumericalSolvers/include/cppoptlib/solver/gradientdescentsolver.h"
IKCPPOPT("gd", GradientDescentSolver);

#include "../../CppNumericalSolvers/include/cppoptlib/solver/lbfgsbsolver.h"
IKCPPOPT("lbfgsb", LbfgsbSolver);

#include "../../CppNumericalSolvers/include/cppoptlib/solver/lbfgssolver.h"
IKCPPOPT("lbfgs", LbfgsSolver);

#include "../../CppNumericalSolvers/include/cppoptlib/solver/neldermeadsolver.h"
IKCPPOPT("nm", NelderMeadSolver);

#include "../../CppNumericalSolvers/include/cppoptlib/solver/newtondescentsolver.h"
IKCPPOPT("nd", NewtonDescentSolver);










