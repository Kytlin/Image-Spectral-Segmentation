#include <iostream>

#include <Eigen/Dense>

int main() {
    // A 3x3 matrix and a vector, filled with the comma initializer (row by row).
    Eigen::Matrix3d A;
    A << 2, -1, 0,
        -1, 2, -1,
         0, -1, 2;
    Eigen::Vector3d x(1, 2, 3);

    std::cout << "A =\n" << A << "\n\n";
    std::cout << "x =\n" << x << "\n\n";

    Eigen::Vector3d y = A * x;
    std::cout << "y =\n" << y << "\n\n";
    std::cout << "Norm of y: " << y.norm() << "\n";

    return 0;
}
