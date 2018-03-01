#pragma once
/**
	@file
	@brief optimal ate pairing over BN-curve
	@author MITSUNARI Shigeo(@herumi)
	@license modified new BSD license
	http://opensource.org/licenses/BSD-3-Clause
*/
#include <mcl/pairing_util.hpp>
#include <assert.h>

//#define MCL_DEV

namespace mcl { namespace bn {

using mcl::CurveParam;

#if 0
const CurveParam CurveFp254BNb = { "-0x4080000000000001", 2, 1, 0 }; // -(2^62 + 2^55 + 1)
// provisional(experimental) param with maxBitSize = 384
const CurveParam CurveFp382_1 = { "-0x400011000000000000000001", 2, 1, 1 }; // -(2^94 + 2^76 + 2^72 + 1) // A Family of Implementation-Friendly BN Elliptic Curves
const CurveParam CurveFp382_2 = { "-0x400040090001000000000001", 2, 1, 2 }; // -(2^94 + 2^78 + 2^67 + 2^64 + 2^48 + 1) // used in relic-toolkit
const CurveParam CurveFp462 = { "0x4001fffffffffffffffffffffbfff", 5, 2, 3 }; // 2^114 + 2^101 - 2^14 - 1 // https://eprint.iacr.org/2017/334
const CurveParam CurveSNARK1 = { "4965661367192848881", 3, 9, 4 };
//const CurveParam CurveSNARK2 = { "4965661367192848881", 82, 9 };
#endif

using mcl::getCurveParam;

template<class Fp>
struct MapToT {
	typedef mcl::Fp2T<Fp> Fp2;
	typedef mcl::EcT<Fp> G1;
	typedef mcl::EcT<Fp2> G2;
	Fp c1; // sqrt(-3)
	Fp c2; // (-1 + sqrt(-3)) / 2
	mpz_class cofactor;
	int legendre(const Fp& x) const
	{
		return gmp::legendre(x.getMpz(), Fp::getOp().mp);
	}
	int legendre(const Fp2& x) const
	{
		Fp y;
		Fp2::norm(y, x);
		return legendre(y);
	}
	void mulFp(Fp& x, const Fp& y) const
	{
		x *= y;
	}
	void mulFp(Fp2& x, const Fp& y) const
	{
		x.a *= y;
		x.b *= y;
	}
	template<class G, class F>
	void calc(G& P, const F& t) const
	{
		F x, y, w;
		bool negative = legendre(t) < 0;
		if (t.isZero()) goto ERR_POINT;
		F::sqr(w, t);
		w += G::b_;
		*w.getFp0() += Fp::one();
		if (w.isZero()) goto ERR_POINT;
		F::inv(w, w);
		mulFp(w, c1);
		w *= t;
		for (int i = 0; i < 3; i++) {
			switch (i) {
			case 0: F::mul(x, t, w); F::neg(x, x); *x.getFp0() += c2; break;
			case 1: F::neg(x, x); *x.getFp0() -= Fp::one(); break;
			case 2: F::sqr(x, w); F::inv(x, x); *x.getFp0() += Fp::one(); break;
			}
			G::getWeierstrass(y, x);
			if (F::squareRoot(y, y)) {
				if (negative) F::neg(y, y);
				P.set(x, y, false);
				return;
			}
		}
	ERR_POINT:
		throw cybozu::Exception("MapToT:calc:bad") << t;
	}
	/*
		cofactor is for G2
	*/
	void init(const mpz_class& cofactor)
	{
		if (!Fp::squareRoot(c1, -3)) throw cybozu::Exception("MapToT:init:c1");
		c2 = (c1 - 1) / 2;
		this->cofactor = cofactor;
	}
	/*
		P.-A. Fouque and M. Tibouchi,
		"Indifferentiable hashing to Barreto Naehrig curves," in Proc. Int. Conf. Cryptol. Inform. Security Latin Amer., 2012, vol. 7533, pp.1-17.

		w = sqrt(-3) t / (1 + b + t^2)
		Remark: throw exception if t = 0, c1, -c1 and b = 2
	*/
	void calcG1(G1& P, const Fp& t) const
	{
		calc<G1, Fp>(P, t);
		assert(P.isValid());
	}
	/*
		get the element in G2 by multiplying the cofactor
	*/
	void calcG2(G2& P, const Fp2& t) const
	{
		calc<G2, Fp2>(P, t);
		assert(cofactor != 0);
		/*
			G2::mul (GLV method) can't be used because P is not on G2
		*/
		G2::mulGeneric(P, P, cofactor);
		assert(!P.isZero());
	}
};

/*
	Software implementation of Attribute-Based Encryption: Appendixes
	GLV for G1
*/
template<class Fp>
struct GLV1 {
	typedef mcl::EcT<Fp> G1;
	Fp rw; // rw = 1 / w = (-1 - sqrt(-3)) / 2
	size_t m;
	mpz_class v0, v1;
	mpz_class B[2][2];
	mpz_class r;
	void init(const mpz_class& r, const mpz_class& z)
	{
		if (!Fp::squareRoot(rw, -3)) throw cybozu::Exception("GLV1:init");
		rw = -(rw + 1) / 2;
		this->r = r;
		m = gmp::getBitSize(r);
		m = (m + fp::UnitBitSize - 1) & ~(fp::UnitBitSize - 1);// a little better size
		v0 = ((6 * z * z + 4 * z + 1) << m) / r;
		v1 = ((-2 * z - 1) << m) / r;
		B[0][0] = 6 * z * z + 2 * z;
		B[0][1] = -2 * z - 1;
		B[1][0] = -2 * z - 1;
		B[1][1] = -6 * z * z - 4 * z - 1;
	}
	/*
		lambda = 36z^4 - 1
		lambda (x, y) = (rw x, y)
	*/
	void mulLambda(G1& Q, const G1& P) const
	{
		Fp::mul(Q.x, P.x, rw);
		Q.y = P.y;
		Q.z = P.z;
	}
	/*
		lambda = 36 z^4 - 1
		x = a + b * lambda mod r
	*/
	void split(mpz_class& a, mpz_class& b, const mpz_class& x) const
	{
		mpz_class t;
		t = (x * v0) >> m;
		b = (x * v1) >> m;
		a = x - (t * B[0][0] + b * B[1][0]);
		b = - (t * B[0][1] + b * B[1][1]);
	}
	void mul(G1& Q, const G1& P, mpz_class x, bool constTime = false) const
	{
		typedef mcl::fp::Unit Unit;
		const size_t maxUnit = 512 / 2 / mcl::fp::UnitBitSize;
		const int splitN = 2;
		mpz_class u[splitN];
		G1 in[splitN];
		G1 tbl[4];
		int bitTbl[splitN]; // bit size of u[i]
		Unit w[splitN][maxUnit]; // unit array of u[i]
		int maxBit = 0; // max bit of u[i]
		int maxN = 0;
		int remainBit = 0;

		x %= r;
		if (x == 0) {
			Q.clear();
			if (constTime) goto DummyLoop;
			return;
		}
		if (x < 0) {
			x += r;
		}
		split(u[0], u[1], x);
		in[0] = P;
		mulLambda(in[1], in[0]);
		for (int i = 0; i < splitN; i++) {
			if (u[i] < 0) {
				u[i] = -u[i];
				G1::neg(in[i], in[i]);
			}
			in[i].normalize();
		}
#if 0
		G1::mulGeneric(in[0], in[0], u[0]);
		G1::mulGeneric(in[1], in[1], u[1]);
		G1::add(Q, in[0], in[1]);
		return;
#else
		tbl[0] = in[0]; // dummy
		tbl[1] = in[0];
		tbl[2] = in[1];
		G1::add(tbl[3], in[0], in[1]);
		tbl[3].normalize();
		for (int i = 0; i < splitN; i++) {
			mcl::gmp::getArray(w[i], maxUnit, u[i]);
			bitTbl[i] = (int)mcl::gmp::getBitSize(u[i]);
			maxBit = std::max(maxBit, bitTbl[i]);
		}
		assert(maxBit > 0);
		maxBit--;
		/*
			maxBit = maxN * UnitBitSize + remainBit
			0 < remainBit <= UnitBitSize
		*/
		maxN = maxBit / mcl::fp::UnitBitSize;
		remainBit = maxBit % mcl::fp::UnitBitSize;
		remainBit++;
		Q.clear();
		for (int i = maxN; i >= 0; i--) {
			for (int j = remainBit - 1; j >= 0; j--) {
				G1::dbl(Q, Q);
				uint32_t b0 = (w[0][i] >> j) & 1;
				uint32_t b1 = (w[1][i] >> j) & 1;
				uint32_t c = b1 * 2 + b0;
				if (c == 0) {
					if (constTime) tbl[0] += tbl[1];
				} else {
					Q += tbl[c];
				}
			}
			remainBit = (int)mcl::fp::UnitBitSize;
		}
#endif
	DummyLoop:
		if (!constTime) return;
		const int limitBit = (int)Fp::getBitSize() / splitN;
		G1 D = tbl[0];
		for (int i = maxBit + 1; i < limitBit; i++) {
			G1::dbl(D, D);
			D += tbl[0];
		}
	}
};

/*
	twisted Frobenius for G2
*/
template<class G2>
struct HaveFrobenius : public G2 {
	typedef typename G2::Fp Fp2;
	static Fp2 g2;
	static Fp2 g3;
	/*
		BN254 is Dtype
		BLS12-381 is Mtype
	*/
	static void init(bool isMtype)
	{
		g2 = Fp2::get_gTbl()[0];
		g3 = Fp2::get_gTbl()[3];
		if (isMtype) {
			Fp2::inv(g2, g2);
			Fp2::inv(g3, g3);
		}
	}
	/*
		FrobeniusOnTwist for Dtype
		p mod 6 = 1, w^6 = xi
		Frob(x', y') = phi Frob phi^-1(x', y')
		= phi Frob (x' w^2, y' w^3)
		= phi (x'^p w^2p, y'^p w^3p)
		= (F(x') w^2(p - 1), F(y') w^3(p - 1))
		= (F(x') g^2, F(y') g^3)

		FrobeniusOnTwist for Dtype
		use (1/g) instead of g
	*/
	static void Frobenius(G2& D, const G2& S)
	{
		Fp2::Frobenius(D.x, S.x);
		Fp2::Frobenius(D.y, S.y);
		Fp2::Frobenius(D.z, S.z);
		D.x *= g2;
		D.y *= g3;
	}
	static void Frobenius(HaveFrobenius& y, const HaveFrobenius& x)
	{
		Frobenius(static_cast<G2&>(y), static_cast<const G2&>(x));
	}
};
template<class G2>
typename G2::Fp HaveFrobenius<G2>::g2;
template<class G2>
typename G2::Fp HaveFrobenius<G2>::g3;

/*
	GLV method for G2 and GT
*/
template<class Fp2>
struct GLV2 {
	typedef typename Fp2::BaseFp Fp;
	typedef mcl::EcT<Fp2> G2;
	typedef mcl::Fp12T<Fp> Fp12;
	size_t m;
	mpz_class B[4][4];
	mpz_class r;
	mpz_class v[4];
	GLV2() : m(0) {}
	void init(const mpz_class& r, const mpz_class& z)
	{
		this->r = r;
		m = mcl::gmp::getBitSize(r);
		m = (m + mcl::fp::UnitBitSize - 1) & ~(mcl::fp::UnitBitSize - 1);// a little better size
		/*
			v[] = [1, 0, 0, 0] * B^(-1) = [2z^2+3z+1, 12z^3+8z^2+z, 6z^3+4z^2+z, -(2z+1)]
		*/
		v[0] = ((1 + z * (3 + z * 2)) << m) / r;
		v[1] = ((z * (1 + z * (8 + z * 12))) << m) / r;
		v[2] = ((z * (1 + z * (4 + z * 6))) << m) / r;
		v[3] = -((z * (1 + z * 2)) << m) / r;
		mpz_class z2p1 = z * 2 + 1;
		B[0][0] = z + 1;
		B[0][1] = z;
		B[0][2] = z;
		B[0][3] = -2 * z;
		B[1][0] = z2p1;
		B[1][1] = -z;
		B[1][2] = -(z + 1);
		B[1][3] = -z;
		B[2][0] = 2 * z;
		B[2][1] = z2p1;
		B[2][2] = z2p1;
		B[2][3] = z2p1;
		B[3][0] = z - 1;
		B[3][1] = 2 * z2p1;
		B[3][2] =  -2 * z + 1;
		B[3][3] = z - 1;
	}
	/*
		u[] = [x, 0, 0, 0] - v[] * x * B
	*/
	void split(mpz_class u[4], const mpz_class& x) const
	{
		mpz_class t[4];
		for (int i = 0; i < 4; i++) {
			t[i] = (x * v[i]) >> m;
		}
		for (int i = 0; i < 4; i++) {
			u[i] = (i == 0) ? x : 0;
			for (int j = 0; j < 4; j++) {
				u[i] -= t[j] * B[j][i];
			}
		}
	}
	template<class T>
	void mul(T& Q, const T& P, mpz_class x, bool constTime = false) const
	{
#if 0 // #ifndef NDEBUG
		{
			T R;
			T::mulGeneric(R, P, r);
			assert(R.isZero());
		}
#endif
		typedef mcl::fp::Unit Unit;
		const size_t maxUnit = 512 / 2 / mcl::fp::UnitBitSize;
		const int splitN = 4;
		mpz_class u[splitN];
		T in[splitN];
		T tbl[16];
		int bitTbl[splitN]; // bit size of u[i]
		Unit w[splitN][maxUnit]; // unit array of u[i]
		int maxBit = 0; // max bit of u[i]
		int maxN = 0;
		int remainBit = 0;

		x %= r;
		if (x == 0) {
			Q.clear();
			if (constTime) goto DummyLoop;
			return;
		}
		if (x < 0) {
			x += r;
		}
		split(u, x);
		in[0] = P;
		T::Frobenius(in[1], in[0]);
		T::Frobenius(in[2], in[1]);
		T::Frobenius(in[3], in[2]);
		for (int i = 0; i < splitN; i++) {
			if (u[i] < 0) {
				u[i] = -u[i];
				T::neg(in[i], in[i]);
			}
//			in[i].normalize(); // slow
		}
#if 0
		for (int i = 0; i < splitN; i++) {
			T::mulGeneric(in[i], in[i], u[i]);
		}
		T::add(Q, in[0], in[1]);
		Q += in[2];
		Q += in[3];
		return;
#else
		tbl[0] = in[0];
		for (size_t i = 1; i < 16; i++) {
			tbl[i].clear();
			if (i & 1) {
				tbl[i] += in[0];
			}
			if (i & 2) {
				tbl[i] += in[1];
			}
			if (i & 4) {
				tbl[i] += in[2];
			}
			if (i & 8) {
				tbl[i] += in[3];
			}
//			tbl[i].normalize();
		}
		for (int i = 0; i < splitN; i++) {
			mcl::gmp::getArray(w[i], maxUnit, u[i]);
			bitTbl[i] = (int)mcl::gmp::getBitSize(u[i]);
			maxBit = std::max(maxBit, bitTbl[i]);
		}
		maxBit--;
		/*
			maxBit = maxN * UnitBitSize + remainBit
			0 < remainBit <= UnitBitSize
		*/
		maxN = maxBit / mcl::fp::UnitBitSize;
		remainBit = maxBit % mcl::fp::UnitBitSize;
		remainBit++;
		Q.clear();
		for (int i = maxN; i >= 0; i--) {
			for (int j = remainBit - 1; j >= 0; j--) {
				T::dbl(Q, Q);
				uint32_t b0 = (w[0][i] >> j) & 1;
				uint32_t b1 = (w[1][i] >> j) & 1;
				uint32_t b2 = (w[2][i] >> j) & 1;
				uint32_t b3 = (w[3][i] >> j) & 1;
				uint32_t c = b3 * 8 + b2 * 4 + b1 * 2 + b0;
				if (c == 0) {
					if (constTime) tbl[0] += tbl[1];
				} else {
					Q += tbl[c];
				}
			}
			remainBit = (int)mcl::fp::UnitBitSize;
		}
#endif
	DummyLoop:
		if (!constTime) return;
		const int limitBit = (int)Fp::getBitSize() / splitN;
		T D = tbl[0];
		for (int i = maxBit + 1; i < limitBit; i++) {
			T::dbl(D, D);
			D += tbl[0];
		}
	}
	void mul(G2& Q, const G2& P, mpz_class x, bool constTime = false) const
	{
		typedef HaveFrobenius<G2> G2withF;
		G2withF& QQ(static_cast<G2withF&>(Q));
		const G2withF& PP(static_cast<const G2withF&>(P));
		mul(QQ, PP, x, constTime);
	}
	void pow(Fp12& z, const Fp12& x, mpz_class y, bool constTime = false) const
	{
		typedef GroupMtoA<Fp12> AG; // as additive group
		AG& _z = static_cast<AG&>(z);
		const AG& _x = static_cast<const AG&>(x);
		mul(_z, _x, y, constTime);
	}
};

template<class Fp>
struct ParamT {
	typedef Fp2T<Fp> Fp2;
	typedef mcl::EcT<Fp> G1;
	typedef mcl::EcT<Fp2> G2;
	int curveType;
	bool isCurveFp254BNb;
	mpz_class z;
	mpz_class abs_z;
	bool isNegative;
	mpz_class p;
	mpz_class r;
	int b;
	bool isMtype; // Dtype if false, BN254 is Dtype, BLS12-381
	/*
		twist
		(x', y') = phi(x, y) = (x/w^2, y/w^3)
		y^2 = x^3 + b
		=> (y'w^3)^2 = (x'w^2)^3 + b
		=> y'^2 = x'^3 + b / w^6 ; w^6 = xi
		=> y'^2 = x'^3 + twist_b;
	*/
	Fp2 twist_b;
	enum {
		tb_generic,
		tb_1m1i,
		tb_1m2i
	} twist_b_type;
	bool is_b_div_xi_1_m1i;
	mpz_class exp_c0;
	mpz_class exp_c1;
	mpz_class exp_c2;
	mpz_class exp_c3;
	MapToT<Fp> mapTo;
	GLV1<Fp> glv1;
	GLV2<Fp2> glv2;

	// Loop parameter for the Miller loop part of opt. ate pairing.
	typedef std::vector<int8_t> SignVec;
	SignVec siTbl;
	size_t precomputedQcoeffSize;
	bool useNAF;
	SignVec zReplTbl;

	void init(const CurveParam& cp = CurveFp254BNb, fp::Mode mode = fp::FP_AUTO)
	{
		curveType = cp.curveType;
		z = mpz_class(cp.z);
		isCurveFp254BNb = cp == CurveFp254BNb;
		bool isBLS12 = false;
		isMtype = false;
		isNegative = z < 0;
		if (isNegative) {
			abs_z = -z;
		} else {
			abs_z = z;
		}
		if (isBLS12) {
			/* BLS12 */
			mpz_class z2 = z * z;
			mpz_class z4 = z2 * z2;
			r = z4 - z2 + 1;
			p = z - 1;
			p = p * p * r / 3 + z;
		} else {
			const int pCoff[] = { 1, 6, 24, 36, 36 };
			const int rCoff[] = { 1, 6, 18, 36, 36 };
			p = eval(pCoff, z);
			assert((p % 6) == 1);
			r = eval(rCoff, z);
		}
		Fp::init(p, mode);
		Fp2::init(cp.xi_a);
		b = cp.b;
		Fp2 xi(cp.xi_a, 1);
		if (isMtype) {
			twist_b = Fp2(b) * xi;
		} else {
			twist_b = Fp2(b) / xi;
		}
		if (twist_b == Fp2(1, -1)) {
			twist_b_type = tb_1m1i;
		} else if (twist_b == Fp2(1, -2)) {
			twist_b_type = tb_1m2i;
		} else {
			twist_b_type = tb_generic;
		}
		G1::init(0, b, mcl::ec::Proj);
		G2::init(0, twist_b, mcl::ec::Proj);
		G2::setOrder(r);
		mapTo.init(2 * p - r);
		glv1.init(r, z);

		const mpz_class largest_c = isBLS12 ? abs_z : gmp::abs(z * 6 + 2);
		useNAF = gmp::getNAF(siTbl, largest_c);
		precomputedQcoeffSize = getPrecomputeQcoeffSize(siTbl);
		gmp::getNAF(zReplTbl, gmp::abs(z));
		if (isBLS12) {
			mpz_class z2 = z * z;
			mpz_class z3 = z2 * z;
			mpz_class z4 = z3 * z;
			mpz_class z5 = z4 * z;
			exp_c0 = z5 - 2 * z4 + 2 * z2 - z + 3;
			exp_c1 = z4 - 2 * z3 + 2 * z - 1;
			exp_c2 = z3 - 2 * z2 + z;
			exp_c3 = z2 - 2 * z + 1;
		} else {
			exp_c0 = -2 + z * (-18 + z * (-30 - 36 *z));
			exp_c1 = 1 + z * (-12 + z * (-18 - 36 * z));
			exp_c2 = 6 * z * z + 1;
		}
	}
	mpz_class eval(const int c[5], const mpz_class& x) const
	{
		return (((c[4] * x + c[3]) * x + c[2]) * x + c[1]) * x + c[0];
	}
	size_t getPrecomputeQcoeffSize(const SignVec& sv) const
	{
		size_t idx = 2 + 2;
		for (size_t i = 2; i < sv.size(); i++) {
			idx++;
			if (sv[i]) idx++;
		}
		return idx;
	}
};

template<class Fp>
struct BNT {
	typedef mcl::Fp2T<Fp> Fp2;
	typedef mcl::Fp6T<Fp> Fp6;
	typedef mcl::Fp12T<Fp> Fp12;
	typedef mcl::EcT<Fp> G1;
	typedef mcl::EcT<Fp2> G2;
	typedef HaveFrobenius<G2> G2withF;
	typedef mcl::FpDblT<Fp> FpDbl;
	typedef mcl::Fp2DblT<Fp> Fp2Dbl;
	typedef ParamT<Fp> Param;
	static Param param;
	static void mulArrayGLV1(G1& z, const G1& x, const mcl::fp::Unit *y, size_t yn, bool isNegative, bool constTime)
	{
		mpz_class s;
		mcl::gmp::setArray(s, y, yn);
		if (isNegative) s = -s;
		param.glv1.mul(z, x, s, constTime);
	}
	static void mulArrayGLV2(G2& z, const G2& x, const mcl::fp::Unit *y, size_t yn, bool isNegative, bool constTime)
	{
		mpz_class s;
		mcl::gmp::setArray(s, y, yn);
		if (isNegative) s = -s;
		param.glv2.mul(z, x, s, constTime);
	}
	static void powArrayGLV2(Fp12& z, const Fp12& x, const mcl::fp::Unit *y, size_t yn, bool isNegative, bool constTime)
	{
		mpz_class s;
		mcl::gmp::setArray(s, y, yn);
		if (isNegative) s = -s;
		param.glv2.pow(z, x, s, constTime);
	}
	static void init(const mcl::bn::CurveParam& cp = CurveFp254BNb, fp::Mode mode = fp::FP_AUTO)
	{
		param.init(cp, mode);
		G1::setMulArrayGLV(mulArrayGLV1);
		param.glv2.init(param.r, param.z);
		G2::setMulArrayGLV(mulArrayGLV2);
		Fp12::setPowArrayGLV(powArrayGLV2);
		const bool isMtype = false;
		G2withF::init(isMtype);
	}
	/*
		l = (a, b, c) => (a, b * P.y, c * P.x)
	*/
	static void updateLine(Fp6& l, const G1& P)
	{
		l.b.a *= P.y;
		l.b.b *= P.y;
		l.c.a *= P.x;
		l.c.b *= P.x;
	}
	static void mul_b_div_xi(Fp2& y, const Fp2& x)
	{
		switch (param.twist_b_type) {
		case Param::tb_1m1i:
			/*
				b / xi = 1 - 1i
				(a + bi)(1 - 1i) = (a + b) + (b - a)i
			*/
			{
				Fp t;
				Fp::add(t, x.a, x.b);
				Fp::sub(y.b, x.b, x.a);
				y.a = t;
			}
			return;
		case Param::tb_1m2i:
			/*
				b / xi = 1 - 2i
				(a + bi)(1 - 2i) = (a + 2b) + (b - 2a)i
			*/
			{
				Fp t;
				Fp::sub(t, x.b, x.a);
				t -= x.a;
				Fp::add(y.a, x.a, x.b);
				y.a += x.b;
				y.b = t;
			}
			return;
		case Param::tb_generic:
			Fp2::mul(y, x, param.twist_b);
			return;
		}
	}
	static void dblLineWithoutP(Fp6& l, G2& Q)
	{
		// 3K x 129
		Fp2 t0, t1, t2, t3, t4, t5;
		Fp2Dbl T0, T1;
		Fp2::sqr(t0, Q.z);
		Fp2::mul(t4, Q.x, Q.y);
		Fp2::sqr(t1, Q.y);
		Fp2::add(t3, t0, t0);
		Fp2::divBy2(t4, t4);
		Fp2::add(t5, t0, t1);
		t0 += t3;
#if 0//#ifdef MCL_DEV
		Fp2::mul_xi(t2, t0);
#else
		mul_b_div_xi(t2, t0);
#endif
		Fp2::sqr(t0, Q.x);
		Fp2::add(t3, t2, t2);
		t3 += t2;
		Fp2::sub(Q.x, t1, t3);
#ifndef MCL_DEV
		Fp2::add(l.c, t0, t0);
		Fp2::add(l.c, l.c, t0);
#endif
		t3 += t1;
		Q.x *= t4;
		Fp2::divBy2(t3, t3);
		Fp2Dbl::sqrPre(T0, t3);
		Fp2Dbl::sqrPre(T1, t2);
		Fp2Dbl::sub(T0, T0, T1);
		Fp2Dbl::add(T1, T1, T1);
		Fp2Dbl::sub(T0, T0, T1);
		Fp2::add(t3, Q.y, Q.z);
		Fp2Dbl::mod(Q.y, T0);
		Fp2::sqr(t3, t3);
		t3 -= t5;
		Fp2::mul(Q.z, t1, t3);
#ifdef MCL_DEV
		Fp2::sub(l.a, t2, t1);
		l.c = t0;
		l.b = t3;
#else
		t2 -= t1;
		Fp2::mul_xi(l.a, t2);
		Fp2::neg(l.b, t3);
#endif
	}
	static void addLineWithoutP(Fp6& l, G2& R, const G2& Q)
	{
		Fp2 t1, t2, t3, t4;
		Fp2Dbl T1, T2;
		Fp2::mul(t1, R.z, Q.x);
		Fp2::mul(t2, R.z, Q.y);
		Fp2::sub(t1, R.x, t1);
		Fp2::sub(t2, R.y, t2);
		Fp2::sqr(t3, t1);
		Fp2::mul(R.x, t3, R.x);
		Fp2::sqr(t4, t2);
		t3 *= t1;
		t4 *= R.z;
		t4 += t3;
		t4 -= R.x;
		t4 -= R.x;
		R.x -= t4;
		Fp2Dbl::mulPre(T1, t2, R.x);
		Fp2Dbl::mulPre(T2, t3, R.y);
		Fp2Dbl::sub(T2, T1, T2);
		Fp2Dbl::mod(R.y, T2);
		Fp2::mul(R.x, t1, t4);
		Fp2::mul(R.z, t3, R.z);
		Fp2::neg(l.c, t2);
		Fp2Dbl::mulPre(T1, t2, Q.x);
		Fp2Dbl::mulPre(T2, t1, Q.y);
		Fp2Dbl::sub(T1, T1, T2);
#ifdef MCL_DEV
		Fp2Dbl::mod(l.a, T1);
#else
		Fp2Dbl::mod(t2, T1);
		Fp2::mul_xi(l.a, t2);
#endif
		l.b = t1;
	}
	static void dblLine(Fp6& l, G2& Q, const G1& P)
	{
		dblLineWithoutP(l, Q);
		updateLine(l, P);
	}
	static void addLine(Fp6& l, G2& R, const G2& Q, const G1& P)
	{
		addLineWithoutP(l, R, Q);
		updateLine(l, P);
	}
	static void mulFp6cb_by_G1xy(Fp6& y, const Fp6& x, const G1& P)
	{
		assert(P.isNormalized());
		if (&y != &x) y.a = x.a;
		Fp2::mulFp(y.c, x.c, P.x);
		Fp2::mulFp(y.b, x.b, P.y);
	}

	static void convertFp6toFp12(Fp12& y, const Fp6& x)
	{
		y.clear();
#ifdef MCL_DEV
		y.a.a = x.b;
		y.b.a = x.c;
		y.b.b = x.a;
#else
		y.a.a = x.a;
		y.a.c = x.c;
		y.b.b = x.b;
#endif
	}
	/*
		x = a + bv + cv^2
		y = (y0, y4, y2) -> (y0, 0, y2, 0, y4, 0)
		z = xy = (a + bv + cv^2)(d + ev)
		= (ad + ce xi) + ((a + b)(d + e) - ad - be)v + (be + cd)v^2
	*/
	static void Fp6mul_01(Fp6& z, const Fp6& x, const Fp2& d, const Fp2& e)
	{
		const Fp2& a = x.a;
		const Fp2& b = x.b;
		const Fp2& c = x.c;
		Fp2 t0, t1;
		Fp2Dbl AD, CE, BE, CD, T;
		Fp2Dbl::mulPre(AD, a, d);
		Fp2Dbl::mulPre(CE, c, e);
		Fp2Dbl::mulPre(BE, b, e);
		Fp2Dbl::mulPre(CD, c, d);
		Fp2::add(t0, a, b);
		Fp2::add(t1, d, e);
		Fp2Dbl::mulPre(T, t0, t1);
		T -= AD;
		T -= BE;
		Fp2Dbl::mod(z.b, T);
		Fp2Dbl::mul_xi(CE, CE);
		AD += CE;
		Fp2Dbl::mod(z.a, AD);
		BE += CD;
		Fp2Dbl::mod(z.c, BE);
	}
	/*
		input
		z = (z0 + z1v + z2v^2) + (z3 + z4v + z5v^2)w = Z0 + Z1w
		x = (a, b, c) -> (b, 0, 0, c, a, 0) = X0 + X1w
		X0 = b = (b, 0, 0)
		X1 = c + av = (c, a, 0)
		w^2 = v, v^3 = xi
		output
		z <- zx = (Z0X0 + Z1X1v) + ((Z0 + Z1)(X0 + X1) - Z0X0 - Z1X1)w
		Z0X0 = Z0 b
		Z1X1 = Z1 (c, a, 0)
		(Z0 + Z1)(X0 + X1) = (Z0 + Z1) (b + c, a, 0)
	*/
	static void mul_025(Fp12& z, const Fp6& x)
	{
		const Fp2& a = x.a;
		const Fp2& b = x.b;
		const Fp2& c = x.c;
#if 0
		Fp6& z0 = z.a;
		Fp6& z1 = z.b;
		Fp6 z0b, z1x1, t0;
		Fp2 t1;
		Fp2::add(t1, x.b, c);
		Fp6::add(t0, z0, z1);
		Fp2::mul(z0b.a, z0.a, b);
		Fp2::mul(z0b.b, z0.b, b);
		Fp2::mul(z0b.c, z0.c, b);
		Fp6mul_01(z1x1, z1, c, a);
		Fp6mul_01(t0, t0, t1, a);
		Fp6::sub(z.b, t0, z0b);
		z.b -= z1x1;
		// a + bv + cv^2 = cxi + av + bv^2
		Fp2::mul_xi(z1x1.c, z1x1.c);
		Fp2::add(z.a.a, z0b.a, z1x1.c);
		Fp2::add(z.a.b, z0b.b, z1x1.a);
		Fp2::add(z.a.c, z0b.c, z1x1.b);
#else
		Fp2& z0 = z.a.a;
		Fp2& z1 = z.a.b;
		Fp2& z2 = z.a.c;
		Fp2& z3 = z.b.a;
		Fp2& z4 = z.b.b;
		Fp2& z5 = z.b.c;
		Fp2Dbl Z0B, Z1B, Z2B, Z3C, Z4C, Z5C;
		Fp2Dbl T0, T1, T2, T3, T4, T5;
		Fp2 bc, t;
		Fp2::addPre(bc, b, c);
		Fp2::addPre(t, z5, z2);
		Fp2Dbl::mulPre(T5, t, bc);
		Fp2Dbl::mulPre(Z5C, z5, c);
		Fp2Dbl::mulPre(Z2B, z2, b);
		Fp2Dbl::sub(T5, T5, Z5C);
		Fp2Dbl::sub(T5, T5, Z2B);
		Fp2Dbl::mulPre(T0, z1, a);
		T5 += T0;

		Fp2::addPre(t, z4, z1);
		Fp2Dbl::mulPre(T4, t, bc);
		Fp2Dbl::mulPre(Z4C, z4, c);
		Fp2Dbl::mulPre(Z1B, z1, b);
		Fp2Dbl::sub(T4, T4, Z4C);
		Fp2Dbl::sub(T4, T4, Z1B);
		Fp2Dbl::mulPre(T0, z0, a);
		T4 += T0;

		Fp2::addPre(t, z3, z0);
		Fp2Dbl::mulPre(T3, t, bc);
		Fp2Dbl::mulPre(Z3C, z3, c);
		Fp2Dbl::mulPre(Z0B, z0, b);
		Fp2Dbl::sub(T3, T3, Z3C);
		Fp2Dbl::sub(T3, T3, Z0B);
		Fp2::mul_xi(t, z2);
		Fp2Dbl::mulPre(T0, t, a);
		T3 += T0;

		Fp2Dbl::mulPre(T2, z3, a);
		T2 += Z2B;
		T2 += Z4C;

		Fp2::mul_xi(t, z5);
		Fp2Dbl::mulPre(T1, t, a);
		T1 += Z1B;
		T1 += Z3C;

		Fp2Dbl::mulPre(T0, z4, a);
		T0 += Z5C;
		Fp2Dbl::mul_xi(T0, T0);
		T0 += Z0B;

		Fp2Dbl::mod(z0, T0);
		Fp2Dbl::mod(z1, T1);
		Fp2Dbl::mod(z2, T2);
		Fp2Dbl::mod(z3, T3);
		Fp2Dbl::mod(z4, T4);
		Fp2Dbl::mod(z5, T5);
#endif
	}
	/*
		input
		z = (z0 + z1v + z2v^2) + (z3 + z4v + z5v^2)w
		x = (a, b, c) -> (a, 0, c, 0, b, 0)
		output
		z <- zx = (z0a + (z1c + z4b)xi) + (z1a + (z2c + z5b)xi)v + (z0c + z2a + z3b)v^2
		+ (z3a + (z2b + z4c)xi)w + (z0b + z4a + z5cxi)vw + (z1b + z3c + z5a)v^2w

		z1c + z4b = (z1 + z4)(c + b) - z1b - z4c
		z2c + z5b = (z2 + z5)(c + b) - z2b - z5c
		z0c + z3b = (z0 + z3)(c + b) - z0b - z3c
	*/
	static void mul_024(Fp12& z, const Fp6& x)
	{
#ifdef MCL_DEV
		mul_025(z, x);
#else
		Fp2& z0 = z.a.a;
		Fp2& z1 = z.a.b;
		Fp2& z2 = z.a.c;
		Fp2& z3 = z.b.a;
		Fp2& z4 = z.b.b;
		Fp2& z5 = z.b.c;
		const Fp2& a = x.a;
		const Fp2& b = x.c;
		const Fp2& c = x.b;
		Fp2 t0, t1, t2;
		Fp2 s0;
		Fp2Dbl T3, T4;
		Fp2Dbl D0, D2, D4;
		Fp2Dbl S1;
		Fp2Dbl::mulPre(D0, z0, a);
		Fp2Dbl::mulPre(D2, z2, b);
		Fp2Dbl::mulPre(D4, z4, c);
		Fp2::add(t2, z0, z4);
		Fp2::add(t1, z0, z2);
		Fp2::add(s0, z1, z3);
		s0 += z5;
		// For z.a.a = z0.
		Fp2Dbl::mulPre(S1, z1, b);
		Fp2Dbl::add(T3, S1, D4);
		Fp2Dbl::mul_xi(T4, T3);
		T4 += D0;
		Fp2Dbl::mod(z0, T4);
		// For z.a.b = z1.
		Fp2Dbl::mulPre(T3, z5, c);
		S1 += T3;
		T3 += D2;
		Fp2Dbl::mul_xi(T4, T3);
		Fp2Dbl::mulPre(T3, z1, a);
		S1 += T3;
		T4 += T3;
		Fp2Dbl::mod(z1, T4);
		// For z.a.c = z2.
		Fp2::add(t0, a, b);
		Fp2Dbl::mulPre(T3, t1, t0);
		T3 -= D0;
		T3 -= D2;
		Fp2Dbl::mulPre(T4, z3, c);
		S1 += T4;
		T3 += T4;
		// z3 needs z2.
		// For z.b.a = z3.
		Fp2::add(t0, z2, z4);
		Fp2Dbl::mod(z2, T3);
		Fp2::add(t1, b, c);
		Fp2Dbl::mulPre(T3, t0, t1);
		T3 -= D2;
		T3 -= D4;
		Fp2Dbl::mul_xi(T4, T3);
		Fp2Dbl::mulPre(T3, z3, a);
		S1 += T3;
		T4 += T3;
		Fp2Dbl::mod(z3, T4);
		// For z.b.b = z4.
		Fp2Dbl::mulPre(T3, z5, b);
		S1 += T3;
		Fp2Dbl::mul_xi(T4, T3);
		Fp2::add(t0, a, c);
		Fp2Dbl::mulPre(T3, t2, t0);
		T3 -= D0;
		T3 -= D4;
		T4 += T3;
		Fp2Dbl::mod(z4, T4);
		// For z.b.c = z5.
		Fp2::add(t0, a, b);
		t0 += c;
		Fp2Dbl::mulPre(T3, s0, t0);
		T3 -= S1;
		Fp2Dbl::mod(z5, T3);
#endif
	}
	static void mul_024_024(Fp12& z, const Fp6& x, const Fp6& y)
	{
		convertFp6toFp12(z, x);
		mul_024(z, y);
	}
	/*
		y = x^d
		d = (p^4 - p^2 + 1)/r = c0 + c1 p + c2 p^2 + p^3
	*/
	static void exp_d(Fp12& y, const Fp12& x)
	{
#if 1
		Fp12 t1, t2, t3;
		Fp12::Frobenius(t1, x);
		Fp12::Frobenius(t2, t1);
		Fp12::Frobenius(t3, t2);
		Fp12::pow(t1, t1, param.exp_c1);
		Fp12::pow(t2, t2, param.exp_c2);
		Fp12::pow(y, x, param.exp_c0);
		y *= t1;
		y *= t2;
		y *= t3;
#else
		const mpz_class& p = param.p;
		mpz_class p2 = p * p;
		mpz_class p4 = p2 * p2;
		Fp12::pow(y, x, (p4 - p2 + 1) / param.r);
#endif
	}
	/*
		Faster Squaring in the Cyclotomic Subgroup of Sixth Degree Extensions
		Robert Granger, Michael Scott
	*/
	static void sqrFp4(Fp2& z0, Fp2& z1, const Fp2& x0, const Fp2& x1)
	{
#if 1
		Fp2Dbl T0, T1, T2;
		Fp2Dbl::sqrPre(T0, x0);
		Fp2Dbl::sqrPre(T1, x1);
		Fp2Dbl::mul_xi(T2, T1);
		Fp2Dbl::add(T2, T2, T0);
		Fp2::add(z1, x0, x1);
		Fp2Dbl::mod(z0, T2);
		Fp2Dbl::sqrPre(T2, z1);
		Fp2Dbl::sub(T2, T2, T0);
		Fp2Dbl::sub(T2, T2, T1);
		Fp2Dbl::mod(z1, T2);
#else
		Fp2 t0, t1, t2;
		Fp2::sqr(t0, x0);
		Fp2::sqr(t1, x1);
		Fp2::mul_xi(z0, t1);
		z0 += t0;
		Fp2::add(z1, x0, x1);
		Fp2::sqr(z1, z1);
		z1 -= t0;
		z1 -= t1;
#endif
	}
	static void fasterSqr(Fp12& y, const Fp12& x)
	{
#if 0
		Fp12::sqr(y, x);
#else
		const Fp2& x0(x.a.a);
		const Fp2& x4(x.a.b);
		const Fp2& x3(x.a.c);
		const Fp2& x2(x.b.a);
		const Fp2& x1(x.b.b);
		const Fp2& x5(x.b.c);
		Fp2& y0(y.a.a);
		Fp2& y4(y.a.b);
		Fp2& y3(y.a.c);
		Fp2& y2(y.b.a);
		Fp2& y1(y.b.b);
		Fp2& y5(y.b.c);
		Fp2 t0, t1;
		sqrFp4(t0, t1, x0, x1);
		Fp2::sub(y0, t0, x0);
		y0 += y0;
		y0 += t0;
		Fp2::add(y1, t1, x1);
		y1 += y1;
		y1 += t1;
		Fp2 t2, t3;
		sqrFp4(t0, t1, x2, x3);
		sqrFp4(t2, t3, x4, x5);
		Fp2::sub(y4, t0, x4);
		y4 += y4;
		y4 += t0;
		Fp2::add(y5, t1, x5);
		y5 += y5;
		y5 += t1;
		Fp2::mul_xi(t0, t3);
		Fp2::add(y2, t0, x2);
		y2 += y2;
		y2 += t0;
		Fp2::sub(y3, t2, x3);
		y3 += y3;
		y3 += t2;
#endif
	}
	struct Compress {
		Fp12& z_;
		Fp2& g1_;
		Fp2& g2_;
		Fp2& g3_;
		Fp2& g4_;
		Fp2& g5_;
		// z is output area
		Compress(Fp12& z, const Fp12& x)
			: z_(z)
			, g1_(z.getFp2()[4])
			, g2_(z.getFp2()[3])
			, g3_(z.getFp2()[2])
			, g4_(z.getFp2()[1])
			, g5_(z.getFp2()[5])
		{
			g2_ = x.getFp2()[3];
			g3_ = x.getFp2()[2];
			g4_ = x.getFp2()[1];
			g5_ = x.getFp2()[5];
		}
		Compress(Fp12& z, const Compress& c)
			: z_(z)
			, g1_(z.getFp2()[4])
			, g2_(z.getFp2()[3])
			, g3_(z.getFp2()[2])
			, g4_(z.getFp2()[1])
			, g5_(z.getFp2()[5])
		{
			g2_ = c.g2_;
			g3_ = c.g3_;
			g4_ = c.g4_;
			g5_ = c.g5_;
		}
		void decompressBeforeInv(Fp2& nume, Fp2& denomi) const
		{
			assert(&nume != &denomi);

			if (g2_.isZero()) {
				Fp2::add(nume, g4_, g4_);
				nume *= g5_;
				denomi = g3_;
			} else {
				Fp2 t;
				Fp2::sqr(nume, g5_);
				Fp2::mul_xi(denomi, nume);
				Fp2::sqr(nume, g4_);
				Fp2::sub(t, nume, g3_);
				t += t;
				t += nume;
				Fp2::add(nume, denomi, t);
				Fp2::divBy4(nume, nume);
				denomi = g2_;
			}
		}

		// output to z
		void decompressAfterInv()
		{
			Fp2& g0 = z_.getFp2()[0];
			Fp2 t0, t1;
			// Compute g0.
			Fp2::sqr(t0, g1_);
			Fp2::mul(t1, g3_, g4_);
			t0 -= t1;
			t0 += t0;
			t0 -= t1;
			Fp2::mul(t1, g2_, g5_);
			t0 += t1;
			Fp2::mul_xi(g0, t0);
			g0.a += Fp::one();
		}

	public:
		void decompress() // for test
		{
			Fp2 nume, denomi;
			decompressBeforeInv(nume, denomi);
			Fp2::inv(denomi, denomi);
			g1_ = nume * denomi; // g1 is recoverd.
			decompressAfterInv();
		}
		/*
			2275clk * 186 = 423Kclk QQQ
		*/
		static void squareC(Compress& z)
		{
			Fp2 t0, t1, t2;
			Fp2Dbl T0, T1, T2, T3;
			Fp2Dbl::sqrPre(T0, z.g4_);
			Fp2Dbl::sqrPre(T1, z.g5_);
			Fp2Dbl::mul_xi(T2, T1);
			T2 += T0;
			Fp2Dbl::mod(t2, T2);
			Fp2::add(t0, z.g4_, z.g5_);
			Fp2Dbl::sqrPre(T2, t0);
			T0 += T1;
			T2 -= T0;
			Fp2Dbl::mod(t0, T2);
			Fp2::add(t1, z.g2_, z.g3_);
			Fp2Dbl::sqrPre(T3, t1);
			Fp2Dbl::sqrPre(T2, z.g2_);
			Fp2::mul_xi(t1, t0);
			z.g2_ += t1;
			z.g2_ += z.g2_;
			z.g2_ += t1;
			Fp2::sub(t1, t2, z.g3_);
			t1 += t1;
			Fp2Dbl::sqrPre(T1, z.g3_);
			Fp2::add(z.g3_, t1, t2);
			Fp2Dbl::mul_xi(T0, T1);
			T0 += T2;
			Fp2Dbl::mod(t0, T0);
			Fp2::sub(z.g4_, t0, z.g4_);
			z.g4_ += z.g4_;
			z.g4_ += t0;
			Fp2Dbl::addPre(T2, T2, T1);
			T3 -= T2;
			Fp2Dbl::mod(t0, T3);
			z.g5_ += t0;
			z.g5_ += z.g5_;
			z.g5_ += t0;
		}
		static void square_n(Compress& z, int n)
		{
			for (int i = 0; i < n; i++) {
				squareC(z);
			}
		}
		/*
			Exponentiation over compression for:
			z = x^Param::z.abs()
		*/
		static void fixed_power(Fp12& z, const Fp12& x)
		{
			if (x.isOne()) {
				z = 1;
				return;
			}
			assert(param.isCurveFp254BNb);
			Fp12 x_org = x;
			Fp12 d62;
			Fp2 c55nume, c55denomi, c62nume, c62denomi;
			Compress c55(z, x);
			Compress::square_n(c55, 55);
			c55.decompressBeforeInv(c55nume, c55denomi);
			Compress c62(d62, c55);
			Compress::square_n(c62, 62 - 55);
			c62.decompressBeforeInv(c62nume, c62denomi);
			Fp2 acc;
			Fp2::mul(acc, c55denomi, c62denomi);
			Fp2::inv(acc, acc);
			Fp2 t;
			Fp2::mul(t, acc, c62denomi);
			Fp2::mul(c55.g1_, c55nume, t);
			c55.decompressAfterInv();
			Fp2::mul(t, acc, c55denomi);
			Fp2::mul(c62.g1_, c62nume, t);
			c62.decompressAfterInv();
			z *= x_org;
			z *= d62;
		}
	};
	/*
		y = x^z if z > 0
		  = unitaryInv(x^(-z)) if z < 0
	*/
	static void pow_z(Fp12& y, const Fp12& x)
	{
#if 1
		if (param.isCurveFp254BNb) {
			Compress::fixed_power(y, x);
		} else {
			Fp12 orgX = x;
			y = x;
			Fp12 conj;
			conj.a = x.a;
			Fp6::neg(conj.b, x.b);
			for (size_t i = 1; i < param.zReplTbl.size(); i++) {
				fasterSqr(y, y);
				if (param.zReplTbl[i] > 0) {
					y *= orgX;
				} else if (param.zReplTbl[i] < 0) {
					y *= conj;
				}
			}
		}
#else
		Fp12::pow(y, x, param.abs_z);
#endif
		if (param.isNegative) {
			Fp12::unitaryInv(y, y);
		}
	}
	/*
		Faster Hashing to G2
		Laura Fuentes-Castaneda, Edward Knapp, Francisco Rodriguez-Henriquez
		section 4.1
		y = x^(d 2z(6z^2 + 3z + 1)) where
		p = p(z) = 36z^4 + 36z^3 + 24z^2 + 6z + 1
		r = r(z) = 36z^4 + 36z^3 + 18z^2 + 6z + 1
		d = (p^4 - p^2 + 1) / r
		d1 = d 2z(6z^2 + 3z + 1)
		= c0 + c1 p + c2 p^2 + c3 p^3

		c0 = 1 + 6z + 12z^2 + 12z^3
		c1 = 4z + 6z^2 + 12z^3
		c2 = 6z + 6z^2 + 12z^3
		c3 = -1 + 4z + 6z^2 + 12z^3
		x -> x^z -> x^2z -> x^4z -> x^6z -> x^(6z^2) -> x^(12z^2) -> x^(12z^3)
		a = x^(6z) x^(6z^2) x^(12z^3)
		b = a / (x^2z)
		x^d1 = (a x^(6z^2) x) b^p a^(p^2) (b / x)^(p^3)
	*/
	static void exp_d1(Fp12& y, const Fp12& x)
	{
		Fp12 a, b;
		Fp12 a2, a3;
		pow_z(b, x); // x^z
		fasterSqr(b, b); // x^2z
		fasterSqr(a, b); // x^4z
		a *= b; // x^6z
		pow_z(a2, a); // x^(6z^2)
		a *= a2;
		fasterSqr(a3, a2); // x^(12z^2)
		pow_z(a3, a3); // x^(12z^3)
		a *= a3;
		Fp12::unitaryInv(b, b);
		b *= a;
		a2 *= a;
		Fp12::Frobenius2(a, a);
		a *= a2;
		a *= x;
		Fp12::unitaryInv(y, x);
		y *= b;
		Fp12::Frobenius(b, b);
		a *= b;
		Fp12::Frobenius3(y, y);
		y *= a;
	}
	static void mapToCyclotomic(Fp12& y, const Fp12& x)
	{
		Fp12 z;
		Fp12::Frobenius2(z, x); // z = x^(p^2)
		z *= x; // x^(p^2 + 1)
		Fp12::inv(y, z);
		Fp6::neg(z.b, z.b); // z^(p^6) = conjugate of z
		y *= z;
	}
	/*
		y = x^((p^12 - 1) / r)
		(p^12 - 1) / r = (p^2 + 1) (p^6 - 1) (p^4 - p^2 + 1)/r
		(a + bw)^(p^6) = a - bw in Fp12
		(p^4 - p^2 + 1)/r = c0 + c1 p + c2 p^2 + p^3
	*/
	static void finalExp(Fp12& y, const Fp12& x)
	{
#if 1
		mapToCyclotomic(y, x);
#else
		const mpz_class& p = param.p;
		mpz_class p2 = p * p;
		mpz_class p4 = p2 * p2;
		Fp12::pow(y, x, p2 + 1);
		Fp12::pow(y, y, p4 * p2 - 1);
#endif
		exp_d1(y, y);
//		exp_d(y, x);
	}
	static G1 makeAdjP(const G1& P)
	{
#ifdef MCL_DEV
		G1 adjP;
		Fp::add(adjP.x, P.x, P.x);
		adjP.x += P.x;
		Fp::neg(adjP.y, P.y);
		adjP.z = 1;
		return adjP;
#else
		return P;
#endif
	}
	static void millerLoop(Fp12& f, const G1& P_, const G2& Q_)
	{
		G1 P(P_);
		G2 Q(Q_);
		P.normalize();
		Q.normalize();
		if (Q.isZero()) {
			f = 1;
			return;
		}
		assert(param.siTbl[1] == 1);
		G2 T = Q;
		G2 negQ;
		if (param.useNAF) {
			G2::neg(negQ, Q);
		}
		Fp6 d, e, l;
		d = e = l = 1;
		G1 adjP = makeAdjP(P);
		dblLine(d, T, adjP);
		addLine(l, T, Q, P);
		mul_024_024(f, d, l);
		for (size_t i = 2; i < param.siTbl.size(); i++) {
			dblLine(l, T, adjP);
			Fp12::sqr(f, f);
			mul_024(f, l);
			if (param.siTbl[i]) {
				if (param.siTbl[i] > 0) {
					addLine(l, T, Q, P);
				} else {
					addLine(l, T, negQ, P);
				}
				mul_024(f, l);
			}
		}
		if (param.z < 0) {
			G2::neg(T, T);
			Fp6::neg(f.b, f.b);
		}
		G2 Q1, Q2;
		G2withF::Frobenius(Q1, Q);
		G2withF::Frobenius(Q2, Q1);
		G2::neg(Q2, Q2);
		addLine(d, T, Q1, P);
		addLine(e, T, Q2, P);
		Fp12 ft;
		mul_024_024(ft, d, e);
		f *= ft;
	}
	static void pairing(Fp12& f, const G1& P, const G2& Q)
	{
		millerLoop(f, P, Q);
		finalExp(f, f);
	}
	/*
		millerLoop(e, P, Q) is same as the following
		std::vector<Fp6> Qcoeff;
		precomputeG2(Qcoeff, Q);
		precomputedMillerLoop(e, P, Qcoeff);
	*/
	static void precomputeG2(std::vector<Fp6>& Qcoeff, const G2& Q)
	{
		Qcoeff.resize(param.precomputedQcoeffSize);
		precomputeG2(Qcoeff.data(), Q);
	}
	/*
		allocate param.precomputedQcoeffSize elements of Fp6 for Qcoeff
	*/
	static void precomputeG2(Fp6 *Qcoeff, const G2& Q_)
	{
		size_t idx = 0;
		G2 Q(Q_);
		Q.normalize();
		if (Q.isZero()) {
			for (size_t i = 0; i < param.precomputedQcoeffSize; i++) {
				Qcoeff[i] = 1;
			}
			return;
		}
		G2 T = Q;
		G2 negQ;
		if (param.useNAF) {
			G2::neg(negQ, Q);
		}
		assert(param.siTbl[1] == 1);
		dblLineWithoutP(Qcoeff[idx++], T);
		addLineWithoutP(Qcoeff[idx++], T, Q);
		for (size_t i = 2; i < param.siTbl.size(); i++) {
			dblLineWithoutP(Qcoeff[idx++], T);
			if (param.siTbl[i]) {
				if (param.siTbl[i] > 0) {
					addLineWithoutP(Qcoeff[idx++], T, Q);
				} else {
					addLineWithoutP(Qcoeff[idx++], T, negQ);
				}
			}
		}
		G2 Q1, Q2;
		G2withF::Frobenius(Q1, Q);
		G2withF::Frobenius(Q2, Q1);
		G2::neg(Q2, Q2);
		if (param.z < 0) {
			G2::neg(T, T);
		}
		addLineWithoutP(Qcoeff[idx++], T, Q1);
		addLineWithoutP(Qcoeff[idx++], T, Q2);
		assert(idx == param.precomputedQcoeffSize);
	}
	static void precomputedMillerLoop(Fp12& f, const G1& P, const std::vector<Fp6>& Qcoeff)
	{
		precomputedMillerLoop(f, P, Qcoeff.data());
	}
	static void precomputedMillerLoop(Fp12& f, const G1& P_, const Fp6* Qcoeff)
	{
		G1 P(P_);
		P.normalize();
		G1 adjP = makeAdjP(P);
		size_t idx = 0;
		Fp6 d, e, l;
		mulFp6cb_by_G1xy(d, Qcoeff[idx], adjP);
		idx++;

		mulFp6cb_by_G1xy(e, Qcoeff[idx], P);
		idx++;
		mul_024_024(f, d, e);
		for (size_t i = 2; i < param.siTbl.size(); i++) {
			mulFp6cb_by_G1xy(l, Qcoeff[idx], adjP);
			idx++;
			Fp12::sqr(f, f);
			mul_024(f, l);
			if (param.siTbl[i]) {
				mulFp6cb_by_G1xy(l, Qcoeff[idx], P);
				idx++;
				mul_024(f, l);
			}
		}
		if (param.z < 0) {
			Fp6::neg(f.b, f.b);
		}
		mulFp6cb_by_G1xy(d, Qcoeff[idx], P);
		idx++;
		mulFp6cb_by_G1xy(e, Qcoeff[idx], P);
		idx++;
		Fp12 ft;
		mul_024_024(ft, d, e);
		f *= ft;
	}
	/*
		f = MillerLoop(P1, Q1) x MillerLoop(P2, Q2)
	*/
	static void precomputedMillerLoop2(Fp12& f, const G1& P1, const std::vector<Fp6>& Q1coeff, const G1& P2, const std::vector<Fp6>& Q2coeff)
	{
		precomputedMillerLoop2(f, P1, Q1coeff.data(), P2, Q2coeff.data());
	}
	static void precomputedMillerLoop2(Fp12& f, const G1& P1_, const Fp6* Q1coeff, const G1& P2_, const Fp6* Q2coeff)
	{
		G1 P1(P1_), P2(P2_);
		P1.normalize();
		P2.normalize();
		G1 adjP1 = makeAdjP(P1);
		G1 adjP2 = makeAdjP(P2);
		size_t idx = 0;
		Fp6 d1, d2, e1, e2, l1, l2;
		mulFp6cb_by_G1xy(d1, Q1coeff[idx], adjP1);
		mulFp6cb_by_G1xy(d2, Q2coeff[idx], adjP2);
		idx++;

		Fp12 f1, f2;
		mulFp6cb_by_G1xy(e1, Q1coeff[idx], P1);
		mul_024_024(f1, d1, e1);

		mulFp6cb_by_G1xy(e2, Q2coeff[idx], P2);
		mul_024_024(f2, d2, e2);
		Fp12::mul(f, f1, f2);
		idx++;
		for (size_t i = 2; i < param.siTbl.size(); i++) {
			mulFp6cb_by_G1xy(l1, Q1coeff[idx], adjP1);
			mulFp6cb_by_G1xy(l2, Q2coeff[idx], adjP2);
			idx++;
			Fp12::sqr(f, f);
			mul_024_024(f1, l1, l2);
			f *= f1;
			if (param.siTbl[i]) {
				mulFp6cb_by_G1xy(l1, Q1coeff[idx], P1);
				mulFp6cb_by_G1xy(l2, Q2coeff[idx], P2);
				idx++;
				mul_024_024(f1, l1, l2);
				f *= f1;
			}
		}
		if (param.z < 0) {
			Fp6::neg(f.b, f.b);
		}
		mulFp6cb_by_G1xy(d1, Q1coeff[idx], P1);
		mulFp6cb_by_G1xy(d2, Q2coeff[idx], P2);
		idx++;
		mulFp6cb_by_G1xy(e1, Q1coeff[idx], P1);
		mulFp6cb_by_G1xy(e2, Q2coeff[idx], P2);
		idx++;
		mul_024_024(f1, d1, e1);
		mul_024_024(f2, d2, e2);
		f *= f1;
		f *= f2;
	}
	static void mapToG1(G1& P, const Fp& x) { param.mapTo.calcG1(P, x); }
	static void mapToG2(G2& P, const Fp2& x) { param.mapTo.calcG2(P, x); }
	static void hashAndMapToG1(G1& P, const void *buf, size_t bufSize)
	{
		Fp t;
		t.setHashOf(buf, bufSize);
		mapToG1(P, t);
	}
	static void hashAndMapToG2(G2& P, const void *buf, size_t bufSize)
	{
		Fp2 t;
		t.a.setHashOf(buf, bufSize);
		t.b.clear();
		mapToG2(P, t);
	}
	static void hashAndMapToG1(G1& P, const std::string& str)
	{
		hashAndMapToG1(P, str.c_str(), str.size());
	}
	static void hashAndMapToG2(G2& P, const std::string& str)
	{
		hashAndMapToG2(P, str.c_str(), str.size());
	}
#if 1 // duplicated later
	// old order of P and Q
	static void pairing(Fp12& f, const G2& Q, const G1& P)
	{
		pairing(f, P, Q);
	}
#endif
};

template<class Fp>
ParamT<Fp> BNT<Fp>::param;

} } // mcl::bn

