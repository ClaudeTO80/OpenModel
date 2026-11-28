#pragma once

/**
 * @file Heat2DLinear.hpp
 * @brief Internal linear algebra shared by the Heat2D solvers: a row-wise sparse matrix, its products, and the
 *        conjugate-gradient solve with Dirichlet rows. Not part of the public API.
 */

#include <cmath>
#include <cstddef>
#include <map>
#include <vector>

namespace OpenModel::Heat2D::detail {

/** @brief Sparse symmetric-in-structure matrix stored by rows. */
struct Sparse {
    std::vector<std::map<int, double>> rows;

    Sparse() = default;
    explicit Sparse(std::size_t n) : rows(n) {}

    void add(int i, int j, double v) { rows[static_cast<std::size_t>(i)][j] += v; }
};

/** @brief a * A + b * B. */
inline Sparse combine(const Sparse& A, double a, const Sparse& B, double b) {
    Sparse C(A.rows.size());
    for (std::size_t i = 0; i < A.rows.size(); ++i) {
        for (const auto& [j, v] : A.rows[i]) C.rows[i][j] += a * v;
        for (const auto& [j, v] : B.rows[i]) C.rows[i][j] += b * v;
    }
    return C;
}

/** @brief y = A x. */
inline std::vector<double> multiply(const Sparse& A, const std::vector<double>& x) {
    std::vector<double> y(A.rows.size(), 0.0);
    for (std::size_t i = 0; i < A.rows.size(); ++i) {
        double s = 0.0;
        for (const auto& [j, v] : A.rows[i]) s += v * x[static_cast<std::size_t>(j)];
        y[i] = s;
    }
    return y;
}

/**
 * @brief Solves A x = b for the symmetric positive-definite matrix, after the fixed nodes are imposed.
 *
 * The fixed rows become identity rows; the columns of the fixed nodes move to the right-hand side so that the system
 * stays symmetric. The solve is conjugate gradient with a Jacobi preconditioner.
 */
inline bool solveFixed(Sparse A, std::vector<double> b, const std::vector<char>& fixed, const std::vector<double>& fixedValue,
                std::vector<double>& x, std::string& err) {
    const std::size_t n = A.rows.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (fixed[i]) continue;
        for (auto it = A.rows[i].begin(); it != A.rows[i].end();) {
            const std::size_t j = static_cast<std::size_t>(it->first);
            if (fixed[j]) {
                b[i] -= it->second * fixedValue[j];
                it = A.rows[i].erase(it);
            } else {
                ++it;
            }
        }
    }
    std::vector<double> diag(n, 1.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (fixed[i]) {
            A.rows[i].clear();
            A.rows[i][static_cast<int>(i)] = 1.0;
            b[i] = fixedValue[i];
        }
        const auto it = A.rows[i].find(static_cast<int>(i));
        if (it == A.rows[i].end() || !(it->second > 0.0)) {
            err = "the linear system is singular: the temperature level is not fixed";
            return false;
        }
        diag[i] = it->second;
    }

    // Conjugate gradient with Jacobi preconditioner, starting from x.
    x.resize(n, 0.0);
    std::vector<double> Ax = multiply(A, x);
    std::vector<double> r(n);
    double bnorm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        r[i] = b[i] - Ax[i];
        bnorm += b[i] * b[i];
    }
    bnorm = std::sqrt(bnorm);
    if (bnorm == 0.0) bnorm = 1.0;

    std::vector<double> z(n);
    for (std::size_t i = 0; i < n; ++i) z[i] = r[i] / diag[i];
    std::vector<double> p = z;
    double rz = 0.0;
    for (std::size_t i = 0; i < n; ++i) rz += r[i] * z[i];

    const int maxIterations = static_cast<int>(20 * n + 1000);
    for (int it = 0; it < maxIterations; ++it) {
        double rnorm = 0.0;
        for (std::size_t i = 0; i < n; ++i) rnorm += r[i] * r[i];
        if (std::sqrt(rnorm) <= 1e-13 * bnorm) return true;

        const std::vector<double> Ap = multiply(A, p);
        double pAp = 0.0;
        for (std::size_t i = 0; i < n; ++i) pAp += p[i] * Ap[i];
        if (!(pAp > 0.0)) {
            err = "the linear system is not positive definite";
            return false;
        }
        const double alpha = rz / pAp;
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * Ap[i];
        }
        for (std::size_t i = 0; i < n; ++i) z[i] = r[i] / diag[i];
        double rzNew = 0.0;
        for (std::size_t i = 0; i < n; ++i) rzNew += r[i] * z[i];
        const double beta = rzNew / rz;
        rz = rzNew;
        for (std::size_t i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    }
    err = "the linear solver did not converge";
    return false;
}

} // namespace OpenModel::Heat2D::detail
