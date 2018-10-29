#ifndef __DERIVS_HXX__
#define __DERIVS_HXX__

#include <iostream>
#include <functional>

#include <bout/assert.hxx>
#include <bout_types.hxx>
#include <stencils.hxx>
#include <bout/template_combinations.hxx>
#include <bout/deriv_store.hxx>

const BoutReal WENO_SMALL = 1.0e-8; // Small number for WENO schemes

struct metaData{
  const std::string key;
  const int nGuards;
  const DERIV derivType; //Can be used to identify the type of the derivative
};

/// Provide an easy way to report a Region's statistics
inline std::ostream &operator<<(std::ostream &out, const metaData &meta){
  out<<"key : "<<meta.key;
  out<<", ";    
  out<<"nGuards : "<<meta.nGuards;
  out<<", ";
  out<<"type : "<<derivToString[meta.derivType];    
  return out;
}

/// Here we define a helper class that provides a means to use a supplied
/// stencil using functor to calculate a derivative over the entire field.
/// Note we currently have a different interface for some of the derivative types
/// to avoid needing different classes to represent the different operations
/// The use of a functor here makes it possible to wrap up metaData into the
/// type as well.
template<typename FF>
class DerivativeType {
public:
  template<DIRECTION direction, STAGGER stagger, typename T>
  void standard(const T &var, T &result, const REGION region) const {
    // A loop over the field setting the stencil, applying the specific method
    // and storing in result -- in this test case we don't have REGION etc. so
    // we comment this out for now.
    //
    ASSERT2(meta.derivType == DERIV::Standard || meta.derivType == DERIV::StandardSecond || meta.derivType == DERIV::StandardFourth)
    ASSERT2(var.getMesh().getNguard<direction>() >= meta.nGuard);
    
    BOUT_FOR(i, var.getRegion(region)) {    
      result[i] = apply(populateStencil<direction, STAGGER::None, 1>(var, i));
    }
    //std::cout<<meta<<" acting on "<<demangler(var)<<" on region "<<region<<std::endl;
    //Note the interface here changes for upwind+flux (although they agree)
    //and then the use in the loop changes between upwind/flux
    //Note also staggered upwind = staggered flux = flux loop
    return;
  }

  template<DIRECTION direction, STAGGER stagger, typename T>
  void upwindOrFlux(const T& vel, const T &var, T &result, const REGION region) const {
    // A loop over the field setting the stencil, applying the specific method
    // and storing in result -- in this test case we don't have REGION etc. so
    // we comment this out for now.
    //
    ASSERT2(meta.derivType == DERIV::Upwind || meta.derivType == DERIV::Flux)    
    ASSERT2(var.getMesh().getNguard<direction>() >= meta.nGuard);
    
    if (meta.derivType == DERIV::Flux || stagger != STAGGER::None) {
      BOUT_FOR(i, var.getRegion(region)) {    
	result[i] = apply(populateStencil<direction, stagger, 1>(vel, i),
			  populateStencil<direction, STAGGER::None, 1>(var, i)
			  );
      }
    } else {
      BOUT_FOR(i, var.getRegion(region)) {    
	result[i] = apply(vel[i],
			  populateStencil<direction, STAGGER::None, 1>(var, i)
			  );
      }
    }
    //std::cout<<meta<<" acting on "<<demangler(var)<<" on region "<<region<<std::endl;
    //Note the interface here changes for upwind+flux (although they agree)
    //and then the use in the loop changes between upwind/flux
    //Note also staggered upwind = staggered flux = flux loop
    return;
  }

  BoutReal apply(const stencil &f) const {
    return func(f);
  }
  BoutReal apply(const BoutReal &v, const stencil &f) const {
    return func(v, f);
  }
  BoutReal apply(const stencil &v, const stencil &f) const {
    return func(v, f);
  }

  FF func{};
  metaData meta = func.meta;

};

#define DEFINE_STANDARD_DERIV(name, key, nGuards, type)	\
  struct name {						\
    BoutReal operator()(const stencil &f) const;	\
    metaData meta = {key, nGuards, type};		\
    BoutReal operator()(const BoutReal &vc, const stencil &f) const{};\
    BoutReal operator()(const stencil &v, const stencil &f) const{};  \
  };							\
  BoutReal name::operator()(const stencil &f) const

#define DEFINE_UPWIND_DERIV(name, key, nGuards, type)			\
  struct name {								\
    BoutReal operator()(const stencil &f) const{};			\
    BoutReal operator()(const BoutReal &vc, const stencil &f) const;	\
    BoutReal operator()(const stencil &v, const stencil &f) const{};	\
    metaData meta = {key, nGuards, type};				\
  };									\
  BoutReal name::operator()(const BoutReal &vc, const stencil &f) const
  
#define DEFINE_FLUX_DERIV(name, key, nGuards, type)			\
  struct name {								\
    BoutReal operator()(const stencil &f) const {};			\
    BoutReal operator()(const BoutReal &vc, const stencil &f) const {};	\
    BoutReal operator()(const stencil &v, const stencil &f) const;	\
    metaData meta = {key, nGuards, type};				\
  };									\
  BoutReal name::operator()(const stencil &v, const stencil &f) const
  
#define DEFINE_STANDARD_DERIV_STAGGERED(name, key, nGuards, type) DEFINE_STANDARD_DERIV(name, key, nGuards, type)
#define DEFINE_UPWIND_DERIV_STAGGERED(name, key, nGuards, type) DEFINE_FLUX_DERIV(name, key, nGuards, type)
#define DEFINE_FLUX_DERIV_STAGGERED(name, key, nGuards, type) DEFINE_FLUX_DERIV(name, key, nGuards, type)

////////////////////// FIRST DERIVATIVES /////////////////////

/// central, 2nd order
DEFINE_STANDARD_DERIV(DDX_C2, "C2", 1, DERIV::Standard) { return 0.5 * (f.p - f.m); };

/// central, 4th order
DEFINE_STANDARD_DERIV(DDX_C4, "C4", 2, DERIV::Standard) { return (8. * f.p - 8. * f.m + f.mm - f.pp) / 12.; }

/// Central WENO method, 2nd order (reverts to 1st order near shocks)
DEFINE_STANDARD_DERIV(DDX_CWENO2, "W2", 1, DERIV::Standard) {
  BoutReal isl, isr, isc;  // Smoothness indicators
  BoutReal al, ar, ac, sa; // Un-normalised weights
  BoutReal dl, dr, dc;     // Derivatives using different stencils

  dc = 0.5 * (f.p - f.m);
  dl = f.c - f.m;
  dr = f.p - f.c;

  isl = SQ(dl);
  isr = SQ(dr);
  isc = (13. / 3.) * SQ(f.p - 2. * f.c + f.m) + 0.25 * SQ(f.p - f.m);

  al = 0.25 / SQ(WENO_SMALL + isl);
  ar = 0.25 / SQ(WENO_SMALL + isr);
  ac = 0.5 / SQ(WENO_SMALL + isc);
  sa = al + ar + ac;

  return (al * dl + ar * dr + ac * dc) / sa;
}

// Smoothing 2nd order derivative
DEFINE_STANDARD_DERIV(DDX_S2, "S2", 2, DERIV::Standard) {

  // 4th-order differencing
  BoutReal result = (8. * f.p - 8. * f.m + f.mm - f.pp) / 12.;

  result += SIGN(f.c) * (f.pp - 4. * f.p + 6. * f.c - 4. * f.m + f.mm) / 12.;

  return result;
}

/// Also CWENO3 but needs an upwind op so define later.

//////////////////////////////
//--- Second order derivatives
//////////////////////////////

/// Second derivative: Central, 2nd order
DEFINE_STANDARD_DERIV(D2DX2_C2, "C2", 1, DERIV::StandardSecond) { return f.p + f.m - 2. * f.c; }

/// Second derivative: Central, 4th order
DEFINE_STANDARD_DERIV(D2DX2_C4, "C4", 2, DERIV::StandardSecond) {return (-f.pp + 16. * f.p - 30. * f.c + 16. * f.m - f.mm) / 12.;}

//////////////////////////////
//--- Fourth order derivatives
//////////////////////////////
DEFINE_STANDARD_DERIV(D4DX4_C2, "C2", 2, DERIV::StandardFourth) { return (f.pp - 4. * f.p + 6. * f.c - 4. * f.m + f.mm); }

////////////////////////////////////////////////////////////////////////////////
/// Upwind non-staggered methods
///
/// Basic derivative methods.
/// All expect to have an input grid cell at the same location as the output
/// Hence convert cell centred values -> centred values, or left -> left
///
////////////////////////////////////////////////////////////////////////////////
std::tuple<BoutReal, BoutReal> vUpDown(const BoutReal v){
  return std::tuple<BoutReal, BoutReal>{ 0.5*(v + fabs(v)), 0.5*(v - fabs(v))};
}

/// Upwinding: Central, 2nd order
DEFINE_UPWIND_DERIV(VDDX_C2, "C2", 1, DERIV::Upwind) { return vc * 0.5 * (f.p - f.m);}

/// Upwinding: Central, 4th order
DEFINE_UPWIND_DERIV(VDDX_C4, "C4", 2, DERIV::Upwind) { return vc * (8. * f.p - 8. * f.m + f.mm - f.pp) / 12.;}

/// upwind, 1st order
DEFINE_UPWIND_DERIV(VDDX_U1, "U1", 1, DERIV::Upwind) { //No vec
  // Existing form doesn't vectorise due to branching
  return vc >= 0.0 ? vc * (f.c - f.m) : vc * (f.p - f.c);
  // Alternative form would but may involve more operations
  const auto vSplit = vUpDown(vc); 
  return (std::get<0>(vSplit)*(f.p-f.c)
	  + std::get<1>(vSplit) * (f.c-f.m));
}

/// upwind, 2nd order
DEFINE_UPWIND_DERIV(VDDX_U2, "U2", 2, DERIV::Upwind) { //No vec
  // Existing form doesn't vectorise due to branching  
  return vc >= 0.0 ? vc * (1.5 * f.c - 2.0 * f.m + 0.5 * f.mm)
    : vc * (-0.5 * f.pp + 2.0 * f.p - 1.5 * f.c);
  // Alternative form would but may involve more operations
  const auto vSplit = vUpDown(vc); 
  return (std::get<0>(vSplit) * (1.5 * f.c - 2.0 * f.m + 0.5 * f.mm)
	  + std::get<1>(vSplit) * (-0.5 * f.pp + 2.0 * f.p - 1.5 * f.c));

}

/// upwind, 3rd order
DEFINE_UPWIND_DERIV(VDDX_U3, "U3", 2, DERIV::Upwind) { //No vec
  // Existing form doesn't vectorise due to branching
  return vc >= 0.0 ? vc*(4.*f.p - 12.*f.m + 2.*f.mm + 6.*f.c)/12.
    : vc*(-4.*f.m + 12.*f.p - 2.*f.pp - 6.*f.c)/12.;
  // Alternative form would but may involve more operations
  const auto vSplit = vUpDown(vc);
  return (std::get<0>(vSplit) * (4.*f.p - 12.*f.m + 2.*f.mm + 6.*f.c)
	  + std::get<1>(vSplit)*(-4.*f.m + 12.*f.p - 2.*f.pp - 6.*f.c))/12.;
  
}

/// 3rd-order WENO scheme
DEFINE_UPWIND_DERIV(VDDX_WENO3, "W3", 2, DERIV::Upwind) { //No vec
  BoutReal deriv, w, r;
  // Existing form doesn't vectorise due to branching
  
  if (vc > 0.0) {
    // Left-biased stencil

    r = (WENO_SMALL + SQ(f.c - 2.0 * f.m + f.mm)) /
        (WENO_SMALL + SQ(f.p - 2.0 * f.c + f.m));

    deriv = (-f.mm + 3. * f.m - 3. * f.c + f.p);

  } else {
    // Right-biased

    r = (WENO_SMALL + SQ(f.pp - 2.0 * f.p + f.c)) /
        (WENO_SMALL + SQ(f.p  - 2.0 * f.c + f.m));

    deriv = (-f.m + 3. * f.c - 3. * f.p + f.pp);
  }
  
  w = 1.0 / (1.0 + 2.0 * r * r);
  deriv = 0.5 * ((f.p - f.m) - w * deriv);
  
  return vc * deriv;
}

///-----------------------------------------------------------------
/// 3rd-order CWENO. Uses the upwinding code and split flux
DEFINE_STANDARD_DERIV(DDX_CWENO3, "W3", 2, DERIV::Standard) {
  BoutReal a, ma = fabs(f.c);
  // Split flux
  a = fabs(f.m);
  if (a > ma)
    ma = a;
  a = fabs(f.p);
  if (a > ma)
    ma = a;
  a = fabs(f.mm);
  if (a > ma)
    ma = a;
  a = fabs(f.pp);
  if (a > ma)
    ma = a;

  stencil sp, sm;

  sp.mm = f.mm + ma;
  sp.m = f.m + ma;
  sp.c = f.c + ma;
  sp.p = f.p + ma;
  sp.pp = f.pp + ma;

  sm.mm = ma - f.mm;
  sm.m = ma - f.m;
  sm.c = ma - f.c;
  sm.p = ma - f.p;
  sm.pp = ma - f.pp;

  const DerivativeType<VDDX_WENO3> upwindOp;
  return upwindOp.apply(0.5, sp) + upwindOp.apply(-0.5, sm);
}

////////////////////////////////////////////////////////////////////////////////
/// Flux non-staggered methods
///
/// Basic derivative methods.
/// All expect to have an input grid cell at the same location as the output
/// Hence convert cell centred values -> centred values, or left -> left
///
////////////////////////////////////////////////////////////////////////////////

DEFINE_FLUX_DERIV(FDDX_U1, "U1", 1, DERIV::Flux) { //No vec

  // Velocity at lower end
  BoutReal vs = 0.5 * (v.m + v.c);
  BoutReal result = (vs >= 0.0) ? vs * f.m : vs * f.c;
  // and at upper
  vs = 0.5 * (v.c + v.p);
  // Existing form doesn't vectorise due to branching
  result -= (vs >= 0.0) ? vs * f.c : vs * f.p;
  return - result;

  // Alternative form would but may involve more operations
  const auto vSplit = vUpDown(vs);
  return result - std::get<0>(vSplit) * f.c + std::get<1>(vSplit) * f.p;
}

DEFINE_FLUX_DERIV(FDDX_C2, "C2", 2, DERIV::Flux) { return 0.5 * (v.p * f.p - v.m * f.m);}

DEFINE_FLUX_DERIV(FDDX_C4, "C4", 2, DERIV::Flux) { return (8. * v.p * f.p - 8. * v.m * f.m + v.mm * f.mm - v.pp * f.pp) / 12.;}

////////////////////////////////////////////////////////////////////////////////
/// Staggered methods
///
/// Map Centre -> Low or Low -> Centre
///
/// These expect the output grid cell to be at a different location to the input
/// 
/// The stencil no longer has a value in 'C' (centre)
/// instead, points are shifted as follows:
///
///  mm  -> -3/2 h
///  m   -> -1/2 h
///  p   -> +1/2 h
///  pp  -? +3/2 h
///
/// NOTE: Cell widths (dx, dy, dz) are currently defined as centre->centre
/// for the methods above. This is currently not taken account of, so large
/// variations in cell size will cause issues.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Standard methods -- first order
////////////////////////////////////////////////////////////////////////////////  
DEFINE_STANDARD_DERIV_STAGGERED(DDX_C2_stag, "C2", 1, DERIV::Standard) { return f.p - f.m; }

DEFINE_STANDARD_DERIV_STAGGERED(DDX_C4_stag, "C4", 2, DERIV::Standard) { return (27. * (f.p - f.m) - (f.pp - f.mm)) / 24.; }

////////////////////////////////////////////////////////////////////////////////
/// Standard methods -- second order
////////////////////////////////////////////////////////////////////////////////  
DEFINE_STANDARD_DERIV_STAGGERED(D2DX2_C2_stag, "C2", 2, DERIV::StandardSecond) { return (f.pp + f.mm - f.p - f.m) / 2.; }

////////////////////////////////////////////////////////////////////////////////
/// Upwind methods
////////////////////////////////////////////////////////////////////////////////
DEFINE_UPWIND_DERIV_STAGGERED(VDDX_U1_stag, "U1", 1, DERIV::Upwind) {
  // Lower cell boundary
  BoutReal result = (v.m >= 0) ? v.m * f.m : v.m * f.c;

  // Upper cell boundary
  result -= (v.p >= 0) ? v.p * f.c : v.p * f.p;
  result *= -1;

  // result is now d/dx(v*f), but want v*d/dx(f) so subtract f*d/dx(v)
  result -= f.c * (v.p - v.m);
  return result;
}

DEFINE_UPWIND_DERIV_STAGGERED(VDDX_U2_stag, "U2", 2, DERIV::Upwind) {
  // Calculate d(v*f)/dx = (v*f)[i+1/2] - (v*f)[i-1/2]

  // Upper cell boundary
  BoutReal result = (v.p >= 0.) ? v.p * (1.5*f.c - 0.5*f.m) : v.p * (1.5*f.p - 0.5*f.pp);

  // Lower cell boundary
  result -= (v.m >= 0.) ? v.m * (1.5*f.m - 0.5*f.mm) : v.m * (1.5*f.c - 0.5*f.p);

  // result is now d/dx(v*f), but want v*d/dx(f) so subtract f*d/dx(v)
  result -= f.c * (v.p - v.m);

  return result;
}

DEFINE_UPWIND_DERIV_STAGGERED(VDDX_C2_stag, "C2", 1, DERIV::Upwind) {
  // Result is needed at location of f: interpolate v to f's location and take an
  // unstaggered derivative of f
  return 0.5 * (v.p + v.m) * 0.5 * (f.p - f.m);
}

DEFINE_UPWIND_DERIV_STAGGERED(VDDX_C4_stag, "C4", 2, DERIV::Upwind) {
  // Result is needed at location of f: interpolate v to f's location and take an
  // unstaggered derivative of f
  return (9. * (v.m + v.p) - v.mm - v.pp) / 16. * (8. * f.p - 8. * f.m + f.mm - f.pp) /
         12.;
}

////////////////////////////////////////////////////////////////////////////////
/// Flux methods
////////////////////////////////////////////////////////////////////////////////
DEFINE_FLUX_DERIV_STAGGERED(FDDX_U1_stag, "U1", 1, DERIV::Flux) {
  // Lower cell boundary
  BoutReal result = (v.m >= 0) ? v.m * f.m : v.m * f.c;

  // Upper cell boundary
  result -= (v.p >= 0) ? v.p * f.c : v.p * f.p;

  return - result;
}

/////////////////////////////////////////////////////////////////////////////////
/// Following code is for dealing with registering a method/methods for all
/// template combinations, in conjunction with the template_combinations code.
/////////////////////////////////////////////////////////////////////////////////

struct registerMethod {
  template<typename Direction, typename Stagger, typename FieldType, typename Method>
  void operator()(Direction, Stagger, FieldType, Method) {
    using namespace std::placeholders;
    
    auto derivativeRegister = DerivativeStore<FieldType>{}.getInstance();
    switch(Method{}.meta.derivType){
    case(DERIV::Standard):
    case(DERIV::StandardSecond):
    case(DERIV::StandardFourth):
      {
	const auto theFunc = std::bind(
				       // Method to store in function
				       &Method::template standard<Direction::value, Stagger::value, FieldType>,
				       // Arguments -- first is hidden this of type-bound, others are placeholders
				       // for input field, output field, region
				       Method{}, _1, _2, _3
				       );
	derivativeRegister.template registerDerivative<Direction, Stagger, Method>(theFunc);
	break;
      }
    case(DERIV::Upwind):
    case(DERIV::Flux):
      {
	const auto theFunc = std::bind(
				       // Method to store in function
				       &Method::template upwindOrFlux<Direction::value, Stagger::value, FieldType>,
				       // Arguments -- first is hidden this of type-bound, others are placeholders
				       // for input field, output field, region
				       Method{}, _1, _2, _3, _4
				       );
	derivativeRegister.template registerDerivative<Direction, Stagger, Method>(theFunc);
	break;
      }
    };
  }
};

/// Some helper defines for now that allow us to wrap up enums
/// and the specific methods.
#define e(family, value) enumWrapper<family, family::value>

/////////////////////////////////////////////////////////////////////////////////
/// Here's an example of registering a couple of DerivativeType methods
/// at once for no staggering
/////////////////////////////////////////////////////////////////////////////////

// Could use Ben's magic macro for thing here to register multiple routines at once
#define REGISTER_DERIVATIVE(name)		\
  namespace {produceCombinations<							\
		       Set<e(DIRECTION,X), e(DIRECTION,Y), e(DIRECTION,Z)>, \
		       Set<e(STAGGER,None)>,				\
		       Set<Field3D, Field2D>,				\
		       Set<DerivativeType<name>>\
									> reg(registerMethod{});}
#define REGISTER_STAGGERED_DERIVATIVE(name)				\
  namespace {produceCombinations<					\
		       Set<e(DIRECTION,X), e(DIRECTION,Y), e(DIRECTION,Z)>, \
         	       Set<e(STAGGER,C2L), e(STAGGER,L2C)>, \
		       Set<Field3D, Field2D>,				\
		       Set<DerivativeType<name>>\
									> reg(registerMethod{});}
  
produceCombinations<
  Set<e(DIRECTION,X), e(DIRECTION,Y), e(DIRECTION,Z)>,
  Set<e(STAGGER,None)>,
  Set<Field3D, Field2D>,
  Set<
    // Standard
    DerivativeType<DDX_C2>,
    DerivativeType<DDX_C4>,
    DerivativeType<DDX_CWENO2>,
    DerivativeType<DDX_S2>,
    DerivativeType<DDX_CWENO3>,
    // Standard 2nd order    
    DerivativeType<D2DX2_C2>,
    DerivativeType<D2DX2_C4>,
    // Standard 4th order    
    DerivativeType<D4DX4_C2>,
    // Upwind
    DerivativeType<VDDX_C2>,
    DerivativeType<VDDX_C4>,
    DerivativeType<VDDX_U1>,
    DerivativeType<VDDX_U2>,
    DerivativeType<VDDX_U3>,        
    DerivativeType<VDDX_WENO3>,
    // Flux
    DerivativeType<FDDX_U1>,
    DerivativeType<FDDX_C2>,
    DerivativeType<FDDX_C4>    
    >
  > registerDerivatives(registerMethod{});

produceCombinations<
  Set<e(DIRECTION,X), e(DIRECTION,Y), e(DIRECTION,Z)>,
  Set<e(STAGGER,C2L), e(STAGGER,L2C)>,
  Set<Field3D, Field2D>,
  Set<
    // Standard
    DerivativeType<DDX_C2_stag>,
    DerivativeType<DDX_C4_stag>,
    // Standard 2nd order    
    DerivativeType<D2DX2_C2_stag>,
    // Upwind
    DerivativeType<VDDX_C2_stag>,
    DerivativeType<VDDX_C4_stag>,
    DerivativeType<VDDX_U1_stag>,
    DerivativeType<VDDX_U2_stag>,
    // Flux
    DerivativeType<FDDX_U1_stag>
    >
  > registerStaggeredDerivatives(registerMethod{});

#endif