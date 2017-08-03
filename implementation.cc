/*
 *    PREDICTOR
 */

START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int nn = 0; nn < nnodes; ++nn) {
  nodal_mass[(nn)] = 0.0;
  nodal_volumes[(nn)] = 0.0;
  nodal_soundspeed[(nn)] = 0.0;
}
STOP_PROFILING(&compute_profile, "zero_node_data");

// Equation of state, ideal gas law
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  pressure0[(cc)] = (GAM - 1.0) * energy0[(cc)] * density0[(cc)];
}
STOP_PROFILING(&compute_profile, "equation_of_state");

// TODO: SOOO MUCH POTENTIAL FOR OPTIMISATION HERE...!
// Calculate the nodal mass
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int nn = 0; nn < nnodes; ++nn) {
  const int node_to_faces_off = nodes_to_faces_offsets[(nn)];
  const int nfaces_by_node =
      nodes_to_faces_offsets[(nn + 1)] - node_to_faces_off;
  const double node_c_x = nodes_x0[(nn)];
  const double node_c_y = nodes_y0[(nn)];
  const double node_c_z = nodes_z0[(nn)];

  // Consider all faces attached to node
  for (int ff = 0; ff < nfaces_by_node; ++ff) {
    const int face_index = nodes_to_faces[(node_to_faces_off + ff)];
    if (face_index == -1) {
      continue;
    }

    // Determine the offset into the list of nodes
    const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
    const int nnodes_by_face =
        faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

    // Find node center and location of current node on face
    int node_in_face_c;
    double face_c_x = 0.0;
    double face_c_y = 0.0;
    double face_c_z = 0.0;
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      const int node_index = faces_to_nodes[(face_to_nodes_off + nn2)];
      face_c_x += nodes_x0[(node_index)] / nnodes_by_face;
      face_c_y += nodes_y0[(node_index)] / nnodes_by_face;
      face_c_z += nodes_z0[(node_index)] / nnodes_by_face;

      // Choose the node in the list of nodes attached to the face
      if (nn == node_index) {
        node_in_face_c = nn2;
      }
    }

    // Fetch the nodes attached to our current node on the current face
    int nodes[2];
    nodes[0] = (node_in_face_c - 1 >= 0)
                   ? faces_to_nodes[(face_to_nodes_off + node_in_face_c - 1)]
                   : faces_to_nodes[(face_to_nodes_off + nnodes_by_face - 1)];
    nodes[1] = (node_in_face_c + 1 < nnodes_by_face)
                   ? faces_to_nodes[(face_to_nodes_off + node_in_face_c + 1)]
                   : faces_to_nodes[(face_to_nodes_off)];

    // Fetch the cells attached to our current face
    int cells[2];
    cells[0] = faces_to_cells0[(face_index)];
    cells[1] = faces_to_cells1[(face_index)];

    // Add contributions from all of the cells attached to the face
    for (int cc = 0; cc < 2; ++cc) {
      if (cells[(cc)] == -1) {
        continue;
      }

      // Add contributions for both edges attached to our current node
      for (int nn2 = 0; nn2 < 2; ++nn2) {
        // Get the halfway point on the right edge
        const double half_edge_x =
            0.5 * (nodes_x0[(nodes[(nn2)])] + nodes_x0[(nn)]);
        const double half_edge_y =
            0.5 * (nodes_y0[(nodes[(nn2)])] + nodes_y0[(nn)]);
        const double half_edge_z =
            0.5 * (nodes_z0[(nodes[(nn2)])] + nodes_z0[(nn)]);

        // Setup basis on plane of tetrahedron
        const double a_x = (face_c_x - node_c_x);
        const double a_y = (face_c_y - node_c_y);
        const double a_z = (face_c_z - node_c_z);
        const double b_x = (face_c_x - half_edge_x);
        const double b_y = (face_c_y - half_edge_y);
        const double b_z = (face_c_z - half_edge_z);
        const double ab_x = (cell_centroids_x[(cells[cc])] - face_c_x);
        const double ab_y = (cell_centroids_y[(cells[cc])] - face_c_y);
        const double ab_z = (cell_centroids_z[(cells[cc])] - face_c_z);

        // Calculate the area vector S using cross product
        double A_x = 0.5 * (a_y * b_z - a_z * b_y);
        double A_y = -0.5 * (a_x * b_z - a_z * b_x);
        double A_z = 0.5 * (a_x * b_y - a_y * b_x);

        // TODO: I HAVENT WORKED OUT A REASONABLE WAY TO ORDER THE NODES
        // SO
        // THAT THIS COMES OUT CORRECTLY, SO NEED TO FIXUP AFTER THE
        // CALCULATION
        const double subcell_volume =
            fabs((ab_x * A_x + ab_y * A_y + ab_z * A_z) / 3.0);

        nodal_mass[(nn)] += density0[(cells[(cc)])] * subcell_volume;
        nodal_soundspeed[(nn)] +=
            sqrt(GAM * (GAM - 1.0) * energy0[(cells[(cc)])]) * subcell_volume;
        nodal_volumes[(nn)] += subcell_volume;
      }
    }
  }
}
STOP_PROFILING(&compute_profile, "calc_nodal_mass_vol");

#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_nodes_off = cells_offsets[(cc)];
  const int nnodes_by_cell = cells_offsets[(cc + 1)] - cell_to_nodes_off;
  for (int nn = 0; nn < nnodes_by_cell; ++nn) {
    subcell_force_x[(cell_to_nodes_off + nn)] = 0.0;
    subcell_force_y[(cell_to_nodes_off + nn)] = 0.0;
    subcell_force_z[(cell_to_nodes_off + nn)] = 0.0;
  }
}

// Calculate the pressure gradients
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_faces_off = cells_to_faces_offsets[(cc)];
  const int nfaces_by_cell =
      cells_to_faces_offsets[(cc + 1)] - cell_to_faces_off;
  const int cell_to_nodes_off = cells_offsets[(cc)];
  const int nnodes_by_cell = cells_offsets[(cc + 1)] - cell_to_nodes_off;

  // Look at all of the faces attached to the cell
  for (int ff = 0; ff < nfaces_by_cell; ++ff) {
    const int face_index = cells_to_faces[(cell_to_faces_off + ff)];
    const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
    const int nnodes_by_face =
        faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

    // Calculate the face center... SHOULD WE PRECOMPUTE?
    double face_c_x = 0.0;
    double face_c_y = 0.0;
    double face_c_z = 0.0;
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      const int node_index = faces_to_nodes[(face_to_nodes_off + nn2)];
      face_c_x += nodes_x0[(node_index)] / nnodes_by_face;
      face_c_y += nodes_y0[(node_index)] / nnodes_by_face;
      face_c_z += nodes_z0[(node_index)] / nnodes_by_face;
    }

    // Now we will sum the contributions at each of the nodes
    // TODO: THERE IS SOME SYMMETRY HERE THAT MEANS WE MIGHT BE ABLE TO
    // OPTIMISE
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      // Fetch the nodes attached to our current node on the current face
      const int current_node = faces_to_nodes[(face_to_nodes_off + nn2)];
      const int next_node = (nn2 + 1 < nnodes_by_face)
                                ? faces_to_nodes[(face_to_nodes_off + nn2 + 1)]
                                : faces_to_nodes[(face_to_nodes_off)];

      // Get the halfway point on the right edge
      const double half_edge_x =
          0.5 * (nodes_x0[(current_node)] + nodes_x0[(next_node)]);
      const double half_edge_y =
          0.5 * (nodes_y0[(current_node)] + nodes_y0[(next_node)]);
      const double half_edge_z =
          0.5 * (nodes_z0[(current_node)] + nodes_z0[(next_node)]);

      // Setup basis on plane of tetrahedron
      const double a_x = (face_c_x - nodes_x0[(current_node)]);
      const double a_y = (face_c_y - nodes_y0[(current_node)]);
      const double a_z = (face_c_z - nodes_z0[(current_node)]);
      const double b_x = (face_c_x - half_edge_x);
      const double b_y = (face_c_y - half_edge_y);
      const double b_z = (face_c_z - half_edge_z);
      const double ab_x = (cell_centroids_x[(cc)] - face_c_x);
      const double ab_y = (cell_centroids_y[(cc)] - face_c_y);
      const double ab_z = (cell_centroids_z[(cc)] - face_c_z);

      // Calculate the area vector S using cross product
      double A_x = 0.5 * (a_y * b_z - a_z * b_y);
      double A_y = -0.5 * (a_x * b_z - a_z * b_x);
      double A_z = 0.5 * (a_x * b_y - a_y * b_x);

      // TODO: I HATE SEARCHES LIKE THIS... CAN WE FIND SOME BETTER CLOSED
      // FORM SOLUTION?
      int node_off;
      int next_node_off;
      for (int nn3 = 0; nn3 < nnodes_by_cell; ++nn3) {
        if (cells_to_nodes[(cell_to_nodes_off + nn3)] == current_node) {
          node_off = nn3;
        } else if (cells_to_nodes[(cell_to_nodes_off + nn3)] == next_node) {
          next_node_off = nn3;
        }
      }

      // TODO: I HAVENT WORKED OUT A REASONABLE WAY TO ORDER THE NODES SO
      // THAT THIS COMES OUT CORRECTLY, SO NEED TO FIXUP AFTER THE
      // CALCULATION
      const int flip = (ab_x * A_x + ab_y * A_y + ab_z * A_z > 0.0);
      subcell_force_x[(cell_to_nodes_off + node_off)] +=
          pressure0[(cc)] * ((flip) ? -A_x : A_x);
      subcell_force_y[(cell_to_nodes_off + node_off)] +=
          pressure0[(cc)] * ((flip) ? -A_y : A_y);
      subcell_force_z[(cell_to_nodes_off + node_off)] +=
          pressure0[(cc)] * ((flip) ? -A_z : A_z);
      subcell_force_x[(cell_to_nodes_off + next_node_off)] +=
          pressure0[(cc)] * ((flip) ? -A_x : A_x);
      subcell_force_y[(cell_to_nodes_off + next_node_off)] +=
          pressure0[(cc)] * ((flip) ? -A_y : A_y);
      subcell_force_z[(cell_to_nodes_off + next_node_off)] +=
          pressure0[(cc)] * ((flip) ? -A_z : A_z);
    }
  }
}
STOP_PROFILING(&compute_profile, "node_force_from_pressure");

START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int nn = 0; nn < nnodes; ++nn) {
  nodal_soundspeed[(nn)] /= nodal_volumes[(nn)];
}
STOP_PROFILING(&compute_profile, "scale_soundspeed");

calc_artificial_viscosity(ncells, visc_coeff1, visc_coeff2, cells_offsets,
                          cells_to_nodes, nodes_x0, nodes_y0, nodes_z0,
                          cell_centroids_x, cell_centroids_y, cell_centroids_z,
                          velocity_x0, velocity_y0, velocity_z0,
                          nodal_soundspeed, nodal_mass, nodal_volumes, limiter,
                          subcell_force_x, subcell_force_y, subcell_force_z,
                          faces_to_nodes_offsets, faces_to_nodes,
                          cells_to_faces_offsets, cells_to_faces);

// Calculate the time centered evolved velocities, by first calculating the
// predicted values at the new timestep and then averaging with current
// velocity
START_PROFILING(&compute_profile);
#pragma omp parallel for simd
for (int nn = 0; nn < nnodes; ++nn) {
  const int node_to_cells_off = nodes_offsets[(nn)];
  const int ncells_by_node = nodes_offsets[(nn + 1)] - node_to_cells_off;

  // Accumulate the force at this node
  double node_force_x0 = 0.0;
  double node_force_y0 = 0.0;
  double node_force_z0 = 0.0;
  for (int cc = 0; cc < ncells_by_node; ++cc) {
    const int cell_index = nodes_to_cells[(node_to_cells_off + cc)];
    const int cell_to_nodes_off = cells_offsets[(cell_index)];
    const int nnodes_by_cell =
        cells_offsets[(cell_index + 1)] - cell_to_nodes_off;

    // ARRGHHHH
    int node_off;
    for (node_off = 0; node_off < nnodes_by_cell; ++node_off) {
      if (cells_to_nodes[(cell_to_nodes_off + node_off)] == nn) {
        break;
      }
    }

    node_force_x0 += subcell_force_x[(cell_to_nodes_off + node_off)];
    node_force_y0 += subcell_force_y[(cell_to_nodes_off + node_off)];
    node_force_z0 += subcell_force_z[(cell_to_nodes_off + node_off)];
  }

  // Determine the predicted velocity
  velocity_x1[(nn)] =
      velocity_x0[(nn)] + mesh->dt * node_force_x0 / nodal_mass[(nn)];
  velocity_y1[(nn)] =
      velocity_y0[(nn)] + mesh->dt * node_force_y0 / nodal_mass[(nn)];
  velocity_z1[(nn)] =
      velocity_z0[(nn)] + mesh->dt * node_force_z0 / nodal_mass[(nn)];

  // Calculate the time centered velocity
  velocity_x1[(nn)] = 0.5 * (velocity_x0[(nn)] + velocity_x1[(nn)]);
  velocity_y1[(nn)] = 0.5 * (velocity_y0[(nn)] + velocity_y1[(nn)]);
  velocity_z1[(nn)] = 0.5 * (velocity_z0[(nn)] + velocity_z1[(nn)]);
}
STOP_PROFILING(&compute_profile, "calc_new_velocity");

// TODO: NEED TO WORK OUT HOW TO HANDLE BOUNDARY CONDITIONS REASONABLY
handle_unstructured_reflect_3d(nnodes, boundary_index, boundary_type,
                               boundary_normal_x, boundary_normal_y,
                               boundary_normal_z, velocity_x1, velocity_y1,
                               velocity_z1);

// Move the nodes by the predicted velocity
START_PROFILING(&compute_profile);
#pragma omp parallel for simd
for (int nn = 0; nn < nnodes; ++nn) {
  nodes_x1[(nn)] = nodes_x0[(nn)] + mesh->dt * velocity_x1[(nn)];
  nodes_y1[(nn)] = nodes_y0[(nn)] + mesh->dt * velocity_y1[(nn)];
  nodes_z1[(nn)] = nodes_z0[(nn)] + mesh->dt * velocity_z1[(nn)];
}
STOP_PROFILING(&compute_profile, "move_nodes");

init_cell_centroids(ncells, cells_offsets, cells_to_nodes, nodes_x1, nodes_y1,
                    nodes_z1, cell_centroids_x, cell_centroids_y,
                    cell_centroids_z);

set_timestep(ncells, nodes_x1, nodes_y1, nodes_z1, energy0, &mesh->dt,
             cells_to_faces_offsets, cells_to_faces, faces_to_nodes_offsets,
             faces_to_nodes);

// Calculate the predicted energy
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_nodes_off = cells_offsets[(cc)];
  const int nnodes_by_cell = cells_offsets[(cc + 1)] - cell_to_nodes_off;

  double cell_force = 0.0;
  for (int nn = 0; nn < nnodes_by_cell; ++nn) {
    const int node_index = cells_to_nodes[(cell_to_nodes_off + nn)];
    cell_force +=
        (velocity_x1[(node_index)] * subcell_force_x[(cell_to_nodes_off + nn)] +
         velocity_y1[(node_index)] * subcell_force_y[(cell_to_nodes_off + nn)] +
         velocity_z1[(node_index)] * subcell_force_z[(cell_to_nodes_off + nn)]);
  }
  energy1[(cc)] = energy0[(cc)] - mesh->dt * cell_force / cell_mass[(cc)];
}
STOP_PROFILING(&compute_profile, "calc_new_energy");

// Using the new volume, calculate the predicted density
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_faces_off = cells_to_faces_offsets[(cc)];
  const int nfaces_by_cell =
      cells_to_faces_offsets[(cc + 1)] - cell_to_faces_off;

  double cell_volume = 0.0;

  // Look at all of the faces attached to the cell
  for (int ff = 0; ff < nfaces_by_cell; ++ff) {
    const int face_index = cells_to_faces[(cell_to_faces_off + ff)];
    const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
    const int nnodes_by_face =
        faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

    // Calculate the face center... SHOULD WE PRECOMPUTE?
    double face_c_x = 0.0;
    double face_c_y = 0.0;
    double face_c_z = 0.0;
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      const int node_index = faces_to_nodes[(face_to_nodes_off + nn2)];
      face_c_x += nodes_x1[(node_index)] / nnodes_by_face;
      face_c_y += nodes_y1[(node_index)] / nnodes_by_face;
      face_c_z += nodes_z1[(node_index)] / nnodes_by_face;
    }

    // Now we will sum the contributions at each of the nodes
    // TODO: THERE IS SOME SYMMETRY HERE THAT MEANS WE MIGHT BE ABLE TO
    // OPTIMISE
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      // Fetch the nodes attached to our current node on the current face
      const int current_node = faces_to_nodes[(face_to_nodes_off + nn2)];
      const int next_node = (nn2 + 1 < nnodes_by_face)
                                ? faces_to_nodes[(face_to_nodes_off + nn2 + 1)]
                                : faces_to_nodes[(face_to_nodes_off)];

      // Get the halfway point on the right edge
      const double half_edge_x =
          0.5 * (nodes_x1[(current_node)] + nodes_x1[(next_node)]);
      const double half_edge_y =
          0.5 * (nodes_y1[(current_node)] + nodes_y1[(next_node)]);
      const double half_edge_z =
          0.5 * (nodes_z1[(current_node)] + nodes_z1[(next_node)]);

      // Setup basis on plane of tetrahedron
      const double a_x = (half_edge_x - face_c_x);
      const double a_y = (half_edge_y - face_c_y);
      const double a_z = (half_edge_z - face_c_z);
      const double b_x = (cell_centroids_x[(cc)] - face_c_x);
      const double b_y = (cell_centroids_y[(cc)] - face_c_y);
      const double b_z = (cell_centroids_z[(cc)] - face_c_z);

      // Calculate the area vector S using cross product
      const double S_x = 0.5 * (a_y * b_z - a_z * b_y);
      const double S_y = -0.5 * (a_x * b_z - a_z * b_x);
      const double S_z = 0.5 * (a_x * b_y - a_y * b_x);

      // TODO: WE MULTIPLY BY 2 HERE BECAUSE WE ARE ADDING THE VOLUME TO BOTH
      // THE CURRENT AND NEXT NODE, OTHERWISE WE ONLY ACCOUNT FOR HALF OF THE
      // 'HALF' TETRAHEDRONS
      cell_volume +=
          fabs(2.0 * ((half_edge_x - nodes_x1[(current_node)]) * S_x +
                      (half_edge_y - nodes_y1[(current_node)]) * S_y +
                      (half_edge_z - nodes_z1[(current_node)]) * S_z) /
               3.0);
    }
  }

  density1[(cc)] = cell_mass[(cc)] / cell_volume;
}
STOP_PROFILING(&compute_profile, "calc_new_density");

// Calculate the time centered pressure from mid point between rezoned and
// predicted pressures
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  // Calculate the predicted pressure from the equation of state
  pressure1[(cc)] = (GAM - 1.0) * energy1[(cc)] * density1[(cc)];

  // Determine the time centered pressure
  pressure1[(cc)] = 0.5 * (pressure0[(cc)] + pressure1[(cc)]);
}
STOP_PROFILING(&compute_profile, "equation_of_state_time_center");

// Prepare time centered variables for the corrector step
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int nn = 0; nn < nnodes; ++nn) {
  nodes_x1[(nn)] = 0.5 * (nodes_x1[(nn)] + nodes_x0[(nn)]);
  nodes_y1[(nn)] = 0.5 * (nodes_y1[(nn)] + nodes_y0[(nn)]);
  nodes_z1[(nn)] = 0.5 * (nodes_z1[(nn)] + nodes_z0[(nn)]);
  nodal_volumes[(nn)] = 0.0;
  nodal_soundspeed[(nn)] = 0.0;
}
STOP_PROFILING(&compute_profile, "move_nodes2");

/*
 *    CORRECTOR
 */

#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_nodes_off = cells_offsets[(cc)];
  const int nnodes_by_cell = cells_offsets[(cc + 1)] - cell_to_nodes_off;
  for (int nn = 0; nn < nnodes_by_cell; ++nn) {
    subcell_force_x[(cell_to_nodes_off + nn)] = 0.0;
    subcell_force_y[(cell_to_nodes_off + nn)] = 0.0;
    subcell_force_z[(cell_to_nodes_off + nn)] = 0.0;
  }
}

// Calculate the nodal mass
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int nn = 0; nn < nnodes; ++nn) {
  const int node_to_faces_off = nodes_to_faces_offsets[(nn)];
  const int nfaces_by_node =
      nodes_to_faces_offsets[(nn + 1)] - node_to_faces_off;
  const double node_c_x = nodes_x1[(nn)];
  const double node_c_y = nodes_y1[(nn)];
  const double node_c_z = nodes_z1[(nn)];

  // Consider all faces attached to node
  for (int ff = 0; ff < nfaces_by_node; ++ff) {
    const int face_index = nodes_to_faces[(node_to_faces_off + ff)];
    if (face_index == -1) {
      continue;
    }

    // Determine the offset into the list of nodes
    const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
    const int nnodes_by_face =
        faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

    // Find node center and location of current node on face
    int node_in_face_c;
    double face_c_x = 0.0;
    double face_c_y = 0.0;
    double face_c_z = 0.0;
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      const int node_index = faces_to_nodes[(face_to_nodes_off + nn2)];
      face_c_x += nodes_x1[(node_index)] / nnodes_by_face;
      face_c_y += nodes_y1[(node_index)] / nnodes_by_face;
      face_c_z += nodes_z1[(node_index)] / nnodes_by_face;

      // Choose the node in the list of nodes attached to the face
      if (nn == node_index) {
        node_in_face_c = nn2;
      }
    }

    // Fetch the nodes attached to our current node on the current face
    int nodes[2];
    nodes[0] = (node_in_face_c - 1 >= 0)
                   ? faces_to_nodes[(face_to_nodes_off + node_in_face_c - 1)]
                   : faces_to_nodes[(face_to_nodes_off + nnodes_by_face - 1)];
    nodes[1] = (node_in_face_c + 1 < nnodes_by_face)
                   ? faces_to_nodes[(face_to_nodes_off + node_in_face_c + 1)]
                   : faces_to_nodes[(face_to_nodes_off)];

    // Fetch the cells attached to our current face
    int cells[2];
    cells[0] = faces_to_cells0[(face_index)];
    cells[1] = faces_to_cells1[(face_index)];

    // Add contributions from all of the cells attached to the face
    for (int cc = 0; cc < 2; ++cc) {
      if (cells[(cc)] == -1) {
        continue;
      }

      // Add contributions for both edges attached to our current node
      for (int nn2 = 0; nn2 < 2; ++nn2) {
        // Get the halfway point on the right edge
        const double half_edge_x =
            0.5 * (nodes_x1[(nodes[(nn2)])] + nodes_x1[(nn)]);
        const double half_edge_y =
            0.5 * (nodes_y1[(nodes[(nn2)])] + nodes_y1[(nn)]);
        const double half_edge_z =
            0.5 * (nodes_z1[(nodes[(nn2)])] + nodes_z1[(nn)]);

        // Setup basis on plane of tetrahedron
        const double a_x = (face_c_x - node_c_x);
        const double a_y = (face_c_y - node_c_y);
        const double a_z = (face_c_z - node_c_z);
        const double b_x = (face_c_x - half_edge_x);
        const double b_y = (face_c_y - half_edge_y);
        const double b_z = (face_c_z - half_edge_z);
        const double ab_x = (cell_centroids_x[(cells[cc])] - face_c_x);
        const double ab_y = (cell_centroids_y[(cells[cc])] - face_c_y);
        const double ab_z = (cell_centroids_z[(cells[cc])] - face_c_z);

        // Calculate the area vector S using cross product
        double A_x = 0.5 * (a_y * b_z - a_z * b_y);
        double A_y = -0.5 * (a_x * b_z - a_z * b_x);
        double A_z = 0.5 * (a_x * b_y - a_y * b_x);

        const double subcell_volume =
            fabs((ab_x * A_x + ab_y * A_y + ab_z * A_z) / 3.0);

        nodal_soundspeed[(nn)] +=
            sqrt(GAM * (GAM - 1.0) * energy1[(cells[(cc)])]) * subcell_volume;
        nodal_volumes[(nn)] += subcell_volume;
      }
    }
  }
}
STOP_PROFILING(&compute_profile, "calc_nodal_mass_vol");

START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int nn = 0; nn < nnodes; ++nn) {
  nodal_soundspeed[(nn)] /= nodal_volumes[(nn)];
}
STOP_PROFILING(&compute_profile, "calc_nodal_soundspeed");

// Calculate the pressure gradients
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_faces_off = cells_to_faces_offsets[(cc)];
  const int nfaces_by_cell =
      cells_to_faces_offsets[(cc + 1)] - cell_to_faces_off;
  const int cell_to_nodes_off = cells_offsets[(cc)];
  const int nnodes_by_cell = cells_offsets[(cc + 1)] - cell_to_nodes_off;

  // Look at all of the faces attached to the cell
  for (int ff = 0; ff < nfaces_by_cell; ++ff) {
    const int face_index = cells_to_faces[(cell_to_faces_off + ff)];
    const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
    const int nnodes_by_face =
        faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

    // Calculate the face center... SHOULD WE PRECOMPUTE?
    double face_c_x = 0.0;
    double face_c_y = 0.0;
    double face_c_z = 0.0;
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      const int node_index = faces_to_nodes[(face_to_nodes_off + nn2)];
      face_c_x += nodes_x1[(node_index)] / nnodes_by_face;
      face_c_y += nodes_y1[(node_index)] / nnodes_by_face;
      face_c_z += nodes_z1[(node_index)] / nnodes_by_face;
    }

    // Now we will sum the contributions at each of the nodes
    // TODO: THERE IS SOME SYMMETRY HERE THAT MEANS WE MIGHT BE ABLE TO
    // OPTIMISE
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      // Fetch the nodes attached to our current node on the current face
      const int current_node = faces_to_nodes[(face_to_nodes_off + nn2)];
      const int next_node = (nn2 + 1 < nnodes_by_face)
                                ? faces_to_nodes[(face_to_nodes_off + nn2 + 1)]
                                : faces_to_nodes[(face_to_nodes_off)];

      // Get the halfway point on the right edge
      const double half_edge_x =
          0.5 * (nodes_x1[(current_node)] + nodes_x1[(next_node)]);
      const double half_edge_y =
          0.5 * (nodes_y1[(current_node)] + nodes_y1[(next_node)]);
      const double half_edge_z =
          0.5 * (nodes_z1[(current_node)] + nodes_z1[(next_node)]);

      // Setup basis on plane of tetrahedron
      const double a_x = (face_c_x - nodes_x1[(current_node)]);
      const double a_y = (face_c_y - nodes_y1[(current_node)]);
      const double a_z = (face_c_z - nodes_z1[(current_node)]);
      const double b_x = (face_c_x - half_edge_x);
      const double b_y = (face_c_y - half_edge_y);
      const double b_z = (face_c_z - half_edge_z);
      const double ab_x = (cell_centroids_x[(cc)] - face_c_x);
      const double ab_y = (cell_centroids_y[(cc)] - face_c_y);
      const double ab_z = (cell_centroids_z[(cc)] - face_c_z);

      // Calculate the area vector S using cross product
      double A_x = 0.5 * (a_y * b_z - a_z * b_y);
      double A_y = -0.5 * (a_x * b_z - a_z * b_x);
      double A_z = 0.5 * (a_x * b_y - a_y * b_x);

      // TODO: I HATE SEARCHES LIKE THIS... CAN WE FIND SOME BETTER CLOSED
      // FORM SOLUTION?
      int node_off;
      int next_node_off;
      for (int nn3 = 0; nn3 < nnodes_by_cell; ++nn3) {
        if (cells_to_nodes[(cell_to_nodes_off + nn3)] == current_node) {
          node_off = nn3;
        } else if (cells_to_nodes[(cell_to_nodes_off + nn3)] == next_node) {
          next_node_off = nn3;
        }
      }

      // TODO: I HAVENT WORKED OUT A REASONABLE WAY TO ORDER THE NODES SO
      // THAT THIS COMES OUT CORRECTLY, SO NEED TO FIXUP AFTER THE
      // CALCULATION
      const int flip = (ab_x * A_x + ab_y * A_y + ab_z * A_z > 0.0);
      subcell_force_x[(cell_to_nodes_off + node_off)] +=
          pressure1[(cc)] * ((flip) ? -A_x : A_x);
      subcell_force_y[(cell_to_nodes_off + node_off)] +=
          pressure1[(cc)] * ((flip) ? -A_y : A_y);
      subcell_force_z[(cell_to_nodes_off + node_off)] +=
          pressure1[(cc)] * ((flip) ? -A_z : A_z);
      subcell_force_x[(cell_to_nodes_off + next_node_off)] +=
          pressure1[(cc)] * ((flip) ? -A_x : A_x);
      subcell_force_y[(cell_to_nodes_off + next_node_off)] +=
          pressure1[(cc)] * ((flip) ? -A_y : A_y);
      subcell_force_z[(cell_to_nodes_off + next_node_off)] +=
          pressure1[(cc)] * ((flip) ? -A_z : A_z);
    }
  }
}
STOP_PROFILING(&compute_profile, "node_force_from_pressure");

calc_artificial_viscosity(ncells, visc_coeff1, visc_coeff2, cells_offsets,
                          cells_to_nodes, nodes_x1, nodes_y1, nodes_z1,
                          cell_centroids_x, cell_centroids_y, cell_centroids_z,
                          velocity_x1, velocity_y1, velocity_z1,
                          nodal_soundspeed, nodal_mass, nodal_volumes, limiter,
                          subcell_force_x, subcell_force_y, subcell_force_z,
                          faces_to_nodes_offsets, faces_to_nodes,
                          cells_to_faces_offsets, cells_to_faces);

START_PROFILING(&compute_profile);
#pragma omp parallel for simd
for (int nn = 0; nn < nnodes; ++nn) {
  const int node_to_cells_off = nodes_offsets[(nn)];
  const int ncells_by_node = nodes_offsets[(nn + 1)] - node_to_cells_off;

  // Consider all faces attached to node
  double node_force_x0 = 0.0;
  double node_force_y0 = 0.0;
  double node_force_z0 = 0.0;
  for (int cc = 0; cc < ncells_by_node; ++cc) {
    const int cell_index = nodes_to_cells[(node_to_cells_off + cc)];
    const int cell_to_nodes_off = cells_offsets[(cell_index)];
    const int nnodes_by_cell =
        cells_offsets[(cell_index + 1)] - cell_to_nodes_off;

    int nn2;
    for (nn2 = 0; nn2 < nnodes_by_cell; ++nn2) {
      if (cells_to_nodes[(cell_to_nodes_off + nn2)] == nn) {
        break;
      }
    }

    node_force_x0 += subcell_force_x[(cell_to_nodes_off + nn2)];
    node_force_y0 += subcell_force_y[(cell_to_nodes_off + nn2)];
    node_force_z0 += subcell_force_z[(cell_to_nodes_off + nn2)];
  }

  // Calculate the new velocities
  velocity_x1[(nn)] += mesh->dt * node_force_x0 / nodal_mass[(nn)];
  velocity_y1[(nn)] += mesh->dt * node_force_y0 / nodal_mass[(nn)];
  velocity_z1[(nn)] += mesh->dt * node_force_z0 / nodal_mass[(nn)];

  // Calculate the corrected time centered velocities
  velocity_x0[(nn)] = 0.5 * (velocity_x1[(nn)] + velocity_x0[(nn)]);
  velocity_y0[(nn)] = 0.5 * (velocity_y1[(nn)] + velocity_y0[(nn)]);
  velocity_z0[(nn)] = 0.5 * (velocity_z1[(nn)] + velocity_z0[(nn)]);
}
STOP_PROFILING(&compute_profile, "calc_new_velocity");

handle_unstructured_reflect_3d(nnodes, boundary_index, boundary_type,
                               boundary_normal_x, boundary_normal_y,
                               boundary_normal_z, velocity_x0, velocity_y0,
                               velocity_z0);

// Calculate the corrected node movements
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int nn = 0; nn < nnodes; ++nn) {
  nodes_x0[(nn)] += mesh->dt * velocity_x0[(nn)];
  nodes_y0[(nn)] += mesh->dt * velocity_y0[(nn)];
  nodes_z0[(nn)] += mesh->dt * velocity_z0[(nn)];
}
STOP_PROFILING(&compute_profile, "move_nodes");

set_timestep(ncells, nodes_x0, nodes_y0, nodes_z0, energy1, &mesh->dt,
             cells_to_faces_offsets, cells_to_faces, faces_to_nodes_offsets,
             faces_to_nodes);

// Calculate the predicted energy
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_nodes_off = cells_offsets[(cc)];
  const int nnodes_by_cell = cells_offsets[(cc + 1)] - cell_to_nodes_off;

  double cell_force = 0.0;
  for (int nn = 0; nn < nnodes_by_cell; ++nn) {
    const int node_index = cells_to_nodes[(cell_to_nodes_off + nn)];
    cell_force +=
        (velocity_x0[(node_index)] * subcell_force_x[(cell_to_nodes_off + nn)] +
         velocity_y0[(node_index)] * subcell_force_y[(cell_to_nodes_off + nn)] +
         velocity_z0[(node_index)] * subcell_force_z[(cell_to_nodes_off + nn)]);
  }

  energy0[(cc)] -= mesh->dt * cell_force / cell_mass[(cc)];
}
STOP_PROFILING(&compute_profile, "calc_new_energy");

init_cell_centroids(ncells, cells_offsets, cells_to_nodes, nodes_x0, nodes_y0,
                    nodes_z0, cell_centroids_x, cell_centroids_y,
                    cell_centroids_z);

// Using the new corrected volume, calculate the density
START_PROFILING(&compute_profile);
#pragma omp parallel for
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_faces_off = cells_to_faces_offsets[(cc)];
  const int nfaces_by_cell =
      cells_to_faces_offsets[(cc + 1)] - cell_to_faces_off;

  double cell_volume = 0.0;

  // Look at all of the faces attached to the cell
  for (int ff = 0; ff < nfaces_by_cell; ++ff) {
    const int face_index = cells_to_faces[(cell_to_faces_off + ff)];
    const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
    const int nnodes_by_face =
        faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

    // Calculate the face center... SHOULD WE PRECOMPUTE?
    double face_c_x = 0.0;
    double face_c_y = 0.0;
    double face_c_z = 0.0;
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      const int node_index = faces_to_nodes[(face_to_nodes_off + nn2)];
      face_c_x += nodes_x0[(node_index)] / nnodes_by_face;
      face_c_y += nodes_y0[(node_index)] / nnodes_by_face;
      face_c_z += nodes_z0[(node_index)] / nnodes_by_face;
    }

    // Now we will sum the contributions at each of the nodes
    // TODO: THERE IS SOME SYMMETRY HERE THAT MEANS WE MIGHT BE ABLE TO
    // OPTIMISE
    for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
      // Fetch the nodes attached to our current node on the current face
      const int current_node = faces_to_nodes[(face_to_nodes_off + nn2)];
      const int next_node = (nn2 + 1 < nnodes_by_face)
                                ? faces_to_nodes[(face_to_nodes_off + nn2 + 1)]
                                : faces_to_nodes[(face_to_nodes_off)];

      // Get the halfway point on the right edge
      const double half_edge_x =
          0.5 * (nodes_x0[(current_node)] + nodes_x0[(next_node)]);
      const double half_edge_y =
          0.5 * (nodes_y0[(current_node)] + nodes_y0[(next_node)]);
      const double half_edge_z =
          0.5 * (nodes_z0[(current_node)] + nodes_z0[(next_node)]);

      // Setup basis on plane of tetrahedron
      const double a_x = (half_edge_x - face_c_x);
      const double a_y = (half_edge_y - face_c_y);
      const double a_z = (half_edge_z - face_c_z);
      const double b_x = (cell_centroids_x[(cc)] - face_c_x);
      const double b_y = (cell_centroids_y[(cc)] - face_c_y);
      const double b_z = (cell_centroids_z[(cc)] - face_c_z);

      // Calculate the area vector S using cross product
      const double S_x = 0.5 * (a_y * b_z - a_z * b_y);
      const double S_y = -0.5 * (a_x * b_z - a_z * b_x);
      const double S_z = 0.5 * (a_x * b_y - a_y * b_x);

      // TODO: WE MULTIPLY BY 2 HERE BECAUSE WE ARE ADDING THE VOLUME TO
      // BOTH
      // THE CURRENT AND NEXT NODE, OTHERWISE WE ONLY ACCOUNT FOR HALF OF
      // THE
      // 'HALF' TETRAHEDRONS
      cell_volume +=
          fabs(2.0 * ((half_edge_x - nodes_x0[(current_node)]) * S_x +
                      (half_edge_y - nodes_y0[(current_node)]) * S_y +
                      (half_edge_z - nodes_z0[(current_node)]) * S_z) /
               3.0);
    }
  }

  // Update the density using the new volume
  density0[(cc)] = cell_mass[(cc)] / cell_volume;
}
STOP_PROFILING(&compute_profile, "calc_new_density");

// Collect the sub-cell centered velocities
for (int cc = 0; cc < ncells; ++cc) {
  const int cell_to_nodes_off = cells_offsets[(cc)];
  const int nnodes_by_cell = cells_offsets[(cc + 1)] - cell_to_nodes_off;

  // Calculate the weighted velocity at the sub-cell center
  double uc_x = 0.0;
  double uc_y = 0.0;
  double uc_z = 0.0;
  for (int nn = 0; nn < nnodes_by_cell; ++nn) {
    const int node_index = (cells_to_nodes[(cell_to_nodes_off + nn)]);
    const int node_to_faces_off = nodes_to_faces_offsets[(node_index)];
    const int nfaces_by_node =
        nodes_to_faces_offsets[(node_index + 1)] - node_to_faces_off;

    int Sn = 0;
    double b_x = 0.0;
    double b_y = 0.0;
    double b_z = 0.0;
    for (int ff = 0; ff < nfaces_by_node; ++ff) {
      const int face_index = nodes_to_faces[(node_to_faces_off + ff)];
      if ((faces_to_cells0[(face_index)] != cc &&
           faces_to_cells1[(face_index)] != cc) ||
          face_index == -1) {
        continue;
      }

      // We have encountered a true face
      Sn += 2;

      const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
      const int nnodes_by_face =
          faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

      // Look at all of the nodes around a face
      int node;
      double f_x = 0.0;
      double f_y = 0.0;
      double f_z = 0.0;
      double face_mass = 0.0;
      for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {
        const int node_index0 = faces_to_nodes[(face_to_nodes_off + nn2)];
        const int node_l_index =
            (nn2 == 0)
                ? faces_to_nodes[(face_to_nodes_off + nnodes_by_face - 1)]
                : faces_to_nodes[(face_to_nodes_off + nn2 - 1)];
        const int node_r_index =
            (nn2 == nnodes_by_face - 1)
                ? faces_to_nodes[(face_to_nodes_off)]
                : faces_to_nodes[(face_to_nodes_off + nn2 + 1)];

        // Add the face center contributions
        double mass = subcell_mass[(cell_to_nodes_off + nn2)];
        f_x += mass * (2.0 * velocity_x0[(node_index0)] -
                       0.5 * velocity_x0[(node_l_index)] -
                       0.5 * velocity_x0[(node_r_index)]);
        f_y += mass * (2.0 * velocity_y0[(node_index0)] -
                       0.5 * velocity_y0[(node_l_index)] -
                       0.5 * velocity_y0[(node_r_index)]);
        f_z += mass * (2.0 * velocity_z0[(node_index0)] -
                       0.5 * velocity_z0[(node_l_index)] -
                       0.5 * velocity_z0[(node_r_index)]);
        face_mass += mass;
        if (node_index0 == node_index) {
          node = nn2;
        }
      }

      const int node_l_index =
          (node == 0) ? faces_to_nodes[(face_to_nodes_off + nnodes_by_face - 1)]
                      : faces_to_nodes[(face_to_nodes_off + node - 1)];
      const int node_r_index =
          (node == nnodes_by_face - 1)
              ? faces_to_nodes[(face_to_nodes_off)]
              : faces_to_nodes[(face_to_nodes_off + node + 1)];

      // Add contributions for right, left, and face center
      b_x += 0.5 * velocity_x0[(node_l_index)] +
             0.5 * velocity_x0[(node_r_index)] + 2.0 * f_x / face_mass;
      b_y += 0.5 * velocity_y0[(node_l_index)] +
             0.5 * velocity_y0[(node_r_index)] + 2.0 * f_y / face_mass;
      b_z += 0.5 * velocity_z0[(node_l_index)] +
             0.5 * velocity_z0[(node_r_index)] + 2.0 * f_z / face_mass;
    }

    double mass = subcell_mass[(cell_to_nodes_off + nn)];
    uc_x +=
        mass * (2.5 * velocity_x0[(node_index)] - (b_x / Sn)) / cell_mass[(cc)];
    uc_y +=
        mass * (2.5 * velocity_y0[(node_index)] - (b_y / Sn)) / cell_mass[(cc)];
    uc_z +=
        mass * (2.5 * velocity_z0[(node_index)] - (b_z / Sn)) / cell_mass[(cc)];
  }

  for (int nn = 0; nn < nnodes_by_cell; ++nn) {
    const int node_index = (cells_to_nodes[(cell_to_nodes_off + nn)]);
    const int node_to_faces_off = nodes_to_faces_offsets[(node_index)];
    const int nfaces_by_node =
        nodes_to_faces_offsets[(node_index + 1)] - node_to_faces_off;

    int Sn = 0;
    double b_x = 0.0;
    double b_y = 0.0;
    double b_z = 0.0;
    for (int ff = 0; ff < nfaces_by_node; ++ff) {
      const int face_index = nodes_to_faces[(node_to_faces_off + ff)];
      if ((faces_to_cells0[(face_index)] != cc &&
           faces_to_cells1[(face_index)] != cc) ||
          face_index == -1) {
        continue;
      }

      // We have encountered a true face
      Sn += 2;

      const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
      const int nnodes_by_face =
          faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

      int node;
      double f_x = 0.0;
      double f_y = 0.0;
      double f_z = 0.0;
      double face_mass = 0.0;
      for (int nn2 = 0; nn2 < nnodes_by_face; ++nn2) {

        const int node_index0 = faces_to_nodes[(face_to_nodes_off + nn2)];
        const int node_l_index =
            (nn2 == 0)
                ? faces_to_nodes[(face_to_nodes_off + nnodes_by_face - 1)]
                : faces_to_nodes[(face_to_nodes_off + nn2 - 1)];
        const int node_r_index =
            (nn2 == nnodes_by_face - 1)
                ? faces_to_nodes[(face_to_nodes_off)]
                : faces_to_nodes[(face_to_nodes_off + nn2 + 1)];

        // Add the face center contributions
        double mass = subcell_mass[(cell_to_nodes_off + nn2)];
        f_x += mass * (2.0 * velocity_x0[(node_index0)] -
                       0.5 * velocity_x0[(node_l_index)] -
                       0.5 * velocity_x0[(node_r_index)]);
        f_y += mass * (2.0 * velocity_y0[(node_index0)] -
                       0.5 * velocity_y0[(node_l_index)] -
                       0.5 * velocity_y0[(node_r_index)]);
        f_z += mass * (2.0 * velocity_z0[(node_index0)] -
                       0.5 * velocity_z0[(node_l_index)] -
                       0.5 * velocity_z0[(node_r_index)]);
        face_mass += mass;

        if (node_index0 == node_index) {
          node = nn2;
        }
      }

      const int node_l_index =
          (node == 0) ? faces_to_nodes[(face_to_nodes_off + nnodes_by_face - 1)]
                      : faces_to_nodes[(face_to_nodes_off + node - 1)];
      const int node_r_index =
          (node == nnodes_by_face - 1)
              ? faces_to_nodes[(face_to_nodes_off)]
              : faces_to_nodes[(face_to_nodes_off + node + 1)];

      // Add right and left node contributions
      b_x += 0.5 * velocity_x0[(node_l_index)] +
             0.5 * velocity_x0[(node_r_index)] + 2.0 * f_x / face_mass;
      b_y += 0.5 * velocity_y0[(node_l_index)] +
             0.5 * velocity_y0[(node_r_index)] + 2.0 * f_y / face_mass;
      b_z += 0.5 * velocity_z0[(node_l_index)] +
             0.5 * velocity_z0[(node_r_index)] + 2.0 * f_z / face_mass;
    }

    // Calculate the final sub-cell velocities
    subcell_velocity_x[(cell_to_nodes_off + nn)] =
        0.25 * (1.5 * velocity_x0[(node_index)] + uc_x + b_x / Sn);
    subcell_velocity_y[(cell_to_nodes_off + nn)] =
        0.25 * (1.5 * velocity_y0[(node_index)] + uc_y + b_y / Sn);
    subcell_velocity_z[(cell_to_nodes_off + nn)] =
        0.25 * (1.5 * velocity_z0[(node_index)] + uc_z + b_z / Sn);
  }
}

/*
*      GATHERING STAGE OF THE REMAP
*/

// Calculate the sub-cell internal energies
for (int cc = 0; cc < ncells; ++cc) {
  // Calculating the volume integrals necessary for the least squares
  // regression
  const int cell_to_faces_off = cells_to_faces_offsets[(cc)];
  const int nfaces_by_cell =
      cells_to_faces_offsets[(cc + 1)] - cell_to_faces_off;
  const int cell_to_nodes_off = cells_offsets[(cc)];
  const int nnodes_by_cell = cells_offsets[(cc + 1)] - cell_to_nodes_off;

  vec_t cell_centroid;
  cell_centroid.x = cell_centroids_x[(cc)];
  cell_centroid.y = cell_centroids_y[(cc)];
  cell_centroid.z = cell_centroids_z[(cc)];

  // The coefficients of the 3x3 gradient coefficient matrix
  vec_t coeff[3] = {{0.0, 0.0, 0.0}};
  vec_t rhs = {0.0, 0.0, 0.0};

  // Determine the weighted volume integrals for neighbouring cells
  for (int ff = 0; ff < nfaces_by_cell; ++ff) {
    const int face_index = cells_to_faces[(cell_to_faces_off + ff)];
    const int neighbour_index = (faces_to_cells0[(face_index)] == cc)
                                    ? faces_to_cells1[(face_index)]
                                    : faces_to_cells0[(face_index)];
    // Check if boundary face
    if (neighbour_index == -1) {
      continue;
    }

    const int neighbour_to_faces_off =
        cells_to_faces_offsets[(neighbour_index)];
    const int nfaces_by_neighbour =
        cells_to_faces_offsets[(neighbour_index + 1)] - neighbour_to_faces_off;

    // Calculate the weighted volume integral coefficients
    double vol = 0.0;
    vec_t integrals;
    vec_t neighbour_centroid = {0.0, 0.0, 0.0};
    neighbour_centroid.x = cell_centroids_x[(neighbour_index)];
    neighbour_centroid.y = cell_centroids_y[(neighbour_index)];
    neighbour_centroid.z = cell_centroids_z[(neighbour_index)];
    calc_weighted_volume_integrals(
        neighbour_to_faces_off, nfaces_by_neighbour, cells_to_faces,
        faces_to_nodes, faces_to_nodes_offsets, nodes_x0, nodes_y0, nodes_z0,
        neighbour_centroid, &integrals, &vol);

    // Complete the integral coefficient as a distance
    integrals.x -= cell_centroid.x * vol;
    integrals.y -= cell_centroid.y * vol;
    integrals.z -= cell_centroid.z * vol;

    // Store the neighbouring cell's contribution to the coefficients
    coeff[0].x += (2.0 * integrals.x * integrals.x) / (vol * vol);
    coeff[0].y += (2.0 * integrals.x * integrals.y) / (vol * vol);
    coeff[0].z += (2.0 * integrals.x * integrals.z) / (vol * vol);

    coeff[1].x += (2.0 * integrals.y * integrals.x) / (vol * vol);
    coeff[1].y += (2.0 * integrals.y * integrals.y) / (vol * vol);
    coeff[1].z += (2.0 * integrals.y * integrals.z) / (vol * vol);

    coeff[2].x += (2.0 * integrals.z * integrals.x) / (vol * vol);
    coeff[2].y += (2.0 * integrals.z * integrals.y) / (vol * vol);
    coeff[2].z += (2.0 * integrals.z * integrals.z) / (vol * vol);

    // Prepare the RHS, which includes energy differential
    const double de = (energy0[(neighbour_index)] - energy0[(cc)]);
    rhs.x += (2.0 * integrals.x * de / vol);
    rhs.y += (2.0 * integrals.y * de / vol);
    rhs.z += (2.0 * integrals.z * de / vol);
  }

  // Determine the inverse of the coefficient matrix
  vec_t inv[3];
  calc_3x3_inverse(&coeff, &inv);

  // Solve for the energy gradient
  vec_t grad_energy;
  grad_energy.x = inv[0].x * rhs.x + inv[0].y * rhs.y + inv[0].z * rhs.z;
  grad_energy.y = inv[1].x * rhs.x + inv[1].y * rhs.y + inv[1].z * rhs.z;
  grad_energy.z = inv[2].x * rhs.x + inv[2].y * rhs.y + inv[2].z * rhs.z;

// Describe the connectivity for a simple tetrahedron, the sub-cell shape
#define NSUBCELL_FACES 4
#define NSUBCELL_NODES 4
#define NSUBCELL_NODES_PER_FACE 3
  const int subcell_faces_to_nodes_offsets[NSUBCELL_FACES + 1] = {0, 3, 6, 9,
                                                                  12};
  const int subcell_faces_to_nodes[NSUBCELL_FACES * NSUBCELL_NODES_PER_FACE] = {
      0, 1, 2, 0, 1, 3, 0, 2, 3, 1, 2, 3};
  const int subcell_to_faces[NSUBCELL_FACES] = {0, 1, 2, 3};
  double subcell_nodes_x[NSUBCELL_NODES] = {0.0};
  double subcell_nodes_y[NSUBCELL_NODES] = {0.0};
  double subcell_nodes_z[NSUBCELL_NODES] = {0.0};

  // The centroid remains a component of all sub-cells
  subcell_nodes_x[3] = cell_centroid.x;
  subcell_nodes_y[3] = cell_centroid.y;
  subcell_nodes_z[3] = cell_centroid.z;

  // Determine the weighted volume integrals for neighbouring cells
  for (int ff = 0; ff < nfaces_by_cell; ++ff) {
    const int face_index = cells_to_faces[(cell_to_faces_off + ff)];
    const int face_to_nodes_off = faces_to_nodes_offsets[(face_index)];
    const int nnodes_by_face =
        faces_to_nodes_offsets[(face_index + 1)] - face_to_nodes_off;

    // The face centroid is the same for all nodes on the face
    subcell_nodes_x[2] = 0.0;
    subcell_nodes_y[2] = 0.0;
    subcell_nodes_z[2] = 0.0;
    for (int nn = 0; nn < nnodes_by_face; ++nn) {
      const int node_index = faces_to_nodes[(face_to_nodes_off + nn)];
      subcell_nodes_x[2] += nodes_x0[(node_index)] / nnodes_by_face;
      subcell_nodes_y[2] += nodes_y0[(node_index)] / nnodes_by_face;
      subcell_nodes_z[2] += nodes_z0[(node_index)] / nnodes_by_face;
    }

    // Each face/node pair has two sub-cells
    for (int nn = 0; nn < nnodes_by_face; ++nn) {
      const int node_index = faces_to_nodes[(face_to_nodes_off + nn)];

      // The left and right nodes on the face for this anchor node
      int nodes_a[2];
      nodes_a[0] =
          (nn == 0) ? faces_to_nodes[(face_to_nodes_off + nnodes_by_face - 1)]
                    : faces_to_nodes[(face_to_nodes_off + nn - 1)];
      nodes_a[1] = (nn == nnodes_by_face - 1)
                       ? faces_to_nodes[(face_to_nodes_off)]
                       : faces_to_nodes[(face_to_nodes_off + nn + 1)];

      for (int ss = 0; ss < 2; ++ss) {
        // Store the right and left nodes
        subcell_nodes_x[1] =
            0.5 * (nodes_x0[nodes_a[ss]] + nodes_x0[(node_index)]);
        subcell_nodes_y[1] =
            0.5 * (nodes_y0[nodes_a[ss]] + nodes_y0[(node_index)]);
        subcell_nodes_z[1] =
            0.5 * (nodes_z0[nodes_a[ss]] + nodes_z0[(node_index)]);

        // Store the anchor node
        subcell_nodes_x[0] = nodes_x0[(node_index)];
        subcell_nodes_y[0] = nodes_y0[(node_index)];
        subcell_nodes_z[0] = nodes_z0[(node_index)];

        // Determine the sub-cell centroid
        vec_t subcell_centroid = {0.0, 0.0, 0.0};
        for (int ii = 0; ii < NSUBCELL_NODES; ++ii) {
          subcell_centroid.x += subcell_nodes_x[ii] / NSUBCELL_NODES;
          subcell_centroid.y += subcell_nodes_y[ii] / NSUBCELL_NODES;
          subcell_centroid.z += subcell_nodes_z[ii] / NSUBCELL_NODES;
        }

        // Calculate the weighted volume integral coefficients
        double vol = 0.0;
        vec_t integrals = {0.0, 0.0, 0.0};
        calc_weighted_volume_integrals(
            0, NSUBCELL_FACES, subcell_to_faces, subcell_faces_to_nodes,
            subcell_faces_to_nodes_offsets, subcell_nodes_x, subcell_nodes_y,
            subcell_nodes_z, subcell_centroid, &integrals, &vol);

        int nn2;
        for (nn2 = 0; nn2 < nnodes_by_cell; ++nn2) {
          if (cells_to_nodes[(cell_to_nodes_off + nn2)] == node_index) {
            break;
          }
        }

        // TODO: THIS MIGHT BE A STUPID WAY TO DO THIS.
        // WE ARE LOOKING AT ALL OF THE SUBCELL TETRAHEDRONS, WHEN WE COULD BE
        // LOOKING AT A SINGLE CORNER SUBCELL PER NODE

        // Store the weighted integrals
        subcell_integrals_x[(cell_to_nodes_off + nn2)] += integrals.x;
        subcell_integrals_y[(cell_to_nodes_off + nn2)] += integrals.y;
        subcell_integrals_z[(cell_to_nodes_off + nn2)] += integrals.z;
        subcell_volume[(cell_to_nodes_off + nn2)] += vol;

        // Determine subcell energy from linear function at cell
        subcell_internal_energy[(cell_to_nodes_off + nn2)] +=
            vol * (density0[(cc)] * energy0[(cc)] -
                   (grad_energy.x * cell_centroid.x +
                    grad_energy.y * cell_centroid.y +
                    grad_energy.z * cell_centroid.z)) +
            grad_energy.x * integrals.x + grad_energy.y * integrals.y +
            grad_energy.z * integrals.z;
      }
    }
  }
}
