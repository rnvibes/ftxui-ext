# KaTeX & LaTeX Math Reference

A collection of mathematical formulations, physical equations, and machine learning architectures for testing and rendering verification in terminal-Unicode FTXUI.

---

## 1. Scaled Dot-Product Attention & Transformers

The core attention mechanism of the Transformer architecture from *Attention Is All You Need* (Vaswani et al., 2017):

$$\text{Attention}(Q, K, V) = \text{softmax}\left(\frac{QK^T}{\sqrt{d_k}}\right)V$$

And Multi-Head Attention:

$$\text{MultiHead}(Q, K, V) = \text{Concat}(\text{head}_1, \dots, \text{head}_h)W^O$$

where each head is computed as:

$$\text{head}_i = \text{Attention}(QW_i^Q, KW_i^K, VW_i^V)$$

---

## 2. Time-Dependent Schrödinger Equation

The fundamental equation of non-relativistic quantum mechanics describing the time evolution of a quantum system's wave function $\Psi(\mathbf{r}, t)$:

$$i\hbar \frac{\partial}{\partial t}\Psi(\mathbf{r}, t) = \hat{H}\Psi(\mathbf{r}, t)$$

Expanding the Hamiltonian operator $\hat{H}$ in terms of kinetic and potential energy:

$$i\hbar \frac{\partial}{\partial t}\Psi(\mathbf{r}, t) = \left[ -\frac{\hbar^2}{2m}\nabla^2 + V(\mathbf{r}, t) \right] \Psi(\mathbf{r}, t)$$

In one spatial dimension:

$$i\hbar \frac{\partial \Psi}{\partial t} = -\frac{\hbar^2}{2m}\frac{\partial^2 \Psi}{\partial x^2} + V(x)\Psi$$

---

## 3. The Lorenz Attractor

The 3-dimensional system of ordinary differential equations first studied by Edward Lorenz for atmospheric convection, exhibiting deterministic chaos:

$$\begin{aligned}
\frac{dx}{dt} &= \sigma (y - x) \\
\frac{dy}{dt} &= x(\rho - z) - y \\
\frac{dz}{dt} &= xy - \beta z
\end{aligned}$$

Standard chaotic parameter values: $\sigma = 10$, $\rho = 28$, $\beta = \frac{8}{3}$.

---

## 4. The Standard Model Lagrangian

The complete Lagrangian density $\mathcal{L}_{\text{SM}}$ of the Standard Model of particle physics, governing the electroweak and strong interactions:

$$\mathcal{L}_{\text{SM}} = \mathcal{L}_{\text{gauge}} + \mathcal{L}_{\text{fermion}} + \mathcal{L}_{\text{Higgs}} + \mathcal{L}_{\text{Yukawa}}$$

### Sector Expansions

1. **Gauge Sector** ($SU(3)_C \times SU(2)_L \times U(1)_Y$ field strength tensors):
$$\mathcal{L}_{\text{gauge}} = -\frac{1}{4}G_{\mu\nu}^a G^{a\mu\nu} - \frac{1}{4}W_{\mu\nu}^I W^{I\mu\nu} - \frac{1}{4}B_{\mu\nu}B^{\mu\nu}$$

2. **Fermion Kinetic and Electroweak Couplings**:
$$\mathcal{L}_{\text{fermion}} = \sum_{\psi} \bar{\psi} i\gamma^\mu D_\mu \psi$$
where the gauge covariant derivative is:
$$D_\mu = \partial_\mu - i g_s G_\mu^a T^a - i g W_\mu^I \tau^I - i g' Y B_\mu$$

3. **Higgs Sector** (electroweak symmetry breaking potential):
$$\mathcal{L}_{\text{Higgs}} = (D_\mu \Phi)^\dagger (D^\mu \Phi) - \mu^2 \Phi^\dagger \Phi - \lambda (\Phi^\dagger \Phi)^2$$

4. **Yukawa Couplings** (fermion mass generation via Higgs VEV):
$$\mathcal{L}_{\text{Yukawa}} = -Y_{ij}^u \bar{Q}_{Li} \tilde{\Phi} u_{Rj} - Y_{ij}^d \bar{Q}_{Li} \Phi d_{Rj} - Y_{ij}^e \bar{L}_{Li} \Phi e_{Rj} + \text{h.c.}$$

---

## 5. Einstein Field Equations

The relationship between spacetime curvature and energy-momentum density in General Relativity:

$$G_{\mu\nu} + \Lambda g_{\mu\nu} = \frac{8\pi G}{c^4} T_{\mu\nu}$$

where the Einstein tensor $G_{\mu\nu}$ is defined by the Ricci curvature tensor $R_{\mu\nu}$ and scalar curvature $R$:

$$G_{\mu\nu} \coloneqq R_{\mu\nu} - \frac{1}{2}R g_{\mu\nu}$$

---

## 6. Matrix & Case Structures

A general $3 \times 3$ rotation matrix in $\mathbb{R}^3$:

$$\mathbf{R}_z(\theta) = \begin{pmatrix}
\cos\theta & -\sin\theta & 0 \\
\sin\theta & \cos\theta & 0 \\
0 & 0 & 1
\end{pmatrix}$$

Piecewise activation function (Leaky ReLU):

$$f(x) = \begin{cases}
x & \text{if } x \ge 0 \\
\alpha x & \text{if } x < 0
\end{cases}$$

---

## 7. Gaussian Normal Distribution & Roots

Probability density function of the univariate normal distribution $\mathcal{N}(\mu, \sigma^2)$:

$$p(x; \mu, \sigma) = \frac{1}{\sigma\sqrt{2\pi}} \exp\left( -\frac{(x - \mu)^2}{2\sigma^2} \right)$$

Higher-order roots:

$$\sqrt[3]{\frac{a^3 + b^3}{c^3}} \le \sqrt[4]{x^4 + y^4}$$
